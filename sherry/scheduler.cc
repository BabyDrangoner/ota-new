#include "scheduler.h"
#include "log.h"
#include "macro.h"
#include "hook.h"
namespace sherry{

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static thread_local Scheduler * t_scheduler = nullptr;
static thread_local Fiber * t_fiber = nullptr;


Scheduler::Scheduler(size_t threads, bool use_caller, const std::string & name)
    :m_name(name){
    SYLAR_ASSERT(threads > 0);

    if(use_caller){

        sherry::Fiber::GetThis();
        --threads;

        SYLAR_ASSERT(GetThis() == nullptr);
        t_scheduler = this;
        m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, true));
        sherry::Thread::SetName(m_name);

        t_fiber = m_rootFiber.get();
        m_rootThread = sherry::GetFiberId();
        m_threadIds.push_back(m_rootThread);
    } else{
        m_rootThread = -1;
    }
    m_threadCount = threads;
}

Scheduler::~Scheduler(){
    SYLAR_ASSERT(m_stopping);

    // 判断当前线程的t_scheduler是否是当前实例
    if(GetThis() == this){
        t_scheduler = nullptr;
    }
}

Scheduler* Scheduler::GetThis(){
    return t_scheduler;
}

Fiber* Scheduler::GetMainFiber(){
    return t_fiber;
}

void Scheduler::start(){
    MutexType::Lock lock(m_mutex);
    if(!m_stopping){
        return;
    }

    // 初始化m_stopping
    m_stopping = false;
    SYLAR_ASSERT(m_threads.empty());

    // 创建线程池
    m_threads.resize(m_threadCount);
    for(size_t i = 0;i < m_threadCount;++i){
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this), m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getId());
    }
    lock.unlock();

    // if(m_rootFiber){
    //     //m_rootFiber->swapIn();
    //     m_rootFiber->call();
    //     SYLAR_LOG_INFO(g_logger) << "call out " << m_rootFiber->getState();
    // }
}

void Scheduler::stop(){
    m_autoStop = true;
    // 当前线程池为空的情况下，更新m_stopping，执行stopping成功后返回
    if(m_rootFiber 
            && m_threadCount == 0 
            && (m_rootFiber->getState() == Fiber::TERM 
                || m_rootFiber->getState() == Fiber::INIT)){
        SYLAR_LOG_INFO(g_logger) << this << " stopped";        
        m_stopping = true;

        if(stopping()){
            return;
        }
    }

    // bool exit_on_this_fiber = false;
    if(m_rootThread != -1){
        SYLAR_ASSERT(GetThis() == this);
    } else {
        SYLAR_ASSERT(GetThis() != this);
    }

    // 否则，唤醒线程并更新m_stopping
    m_stopping = true;
    for(size_t i = 0;i < m_threadCount;++i){
        tickle();
    }

    if(m_rootFiber){
        tickle();
    }

    
    if(m_rootFiber){
        // while(!stopping()){
        //     if(m_rootFiber->getState() == Fiber::TERM
        //         || m_rootFiber->getState() == Fiber::EXCEPT){
        //         m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, true));
        //         SYLAR_LOG_INFO(g_logger) << " root fiber is term, reset";
        //         t_fiber = m_rootFiber.get();
        //     }
        //     m_rootFiber->call();
        // }

        if(!stopping()){
            m_rootFiber->call();
        }
    }

    // 清空线程池并等待线程join
    std::vector<Thread::ptr> thrs;
    {
        MutexType::Lock lock(m_mutex);
        thrs.swap(m_threads);
    }

    for(auto & i : thrs){
        i->join();
    }

    // if(exit_on_this_fiber){
    // }
}

void Scheduler::setThis(){
    t_scheduler = this;
}

void Scheduler::run(){
    SYLAR_LOG_INFO(g_logger) << "run";
    
    // 设置hook使能
    set_hook_enable(true);
    
    // 调用setThis，设置当前线程的调度器
    setThis();

    // 更新当前线程正在运行的协程t_fiber
    if(sherry::GetThreadId() != m_rootThread){
        t_fiber = Fiber::GetThis().get();
    }

    // 初始化当前线程的空闲协程
    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
    Fiber::ptr cb_fiber;  // 表示当前线程在本轮循环中，是否成功从队列中取出任务，在else逻辑中判断是否需要调度idle协程

    FiberAndThread ft;
    while(true){
        ft.reset();
        bool tickle_me = false;
        bool is_active = false;
        // SYLAR_LOG_DEBUG(g_logger) << "fiber:" << sherry::GetFiberId() << " run";
        // 线程尝试取出协程
        {
            MutexType::Lock lock(m_mutex);
            auto it = m_fibers.begin();
            while(it != m_fibers.end()){
                // it任务所属线程不是该线程，continue
                if(it->thread != -1 && it->thread != sherry::GetThreadId()){
                    ++it;
                    tickle_me = true;
                    continue;
                }
                // it任务携带协程或业务函数，否则报错
                SYLAR_ASSERT(it->fiber || it->cb);
                // it任务携带协程，但是协程正在执行，continue
                if(it->fiber && it->fiber->getState() == Fiber::EXEC){
                    ++it;
                    continue;
                }
                // 获取该it任务
                ft = *it;
                m_fibers.erase(it);
                ++m_activeThreadCount;
                is_active = true;
                break;
            }
        }
        
        // 唤醒其他线程
        if(tickle_me){
            tickle();
        }

        // 处理拿到的任务
        // ft任务携带协程，且任务未终止和移除，swapin()执行
        if(ft.fiber && (ft.fiber->getState() != Fiber::TERM
                        && ft.fiber->getState() != Fiber::EXCEPT)){
            // SYLAR_LOG_DEBUG(g_logger) << "before ft.state:" << ft.fiber->getState();
            ft.fiber->swapIn();
            --m_activeThreadCount;
            // 根据协程恢复后的状态，如果READY则继续调度，
            // SYLAR_LOG_DEBUG(g_logger) << "after ft.state:" << ft.fiber->getState();
            if(ft.fiber->getState() == Fiber::READY){
                schedule(ft.fiber);
            } else if(ft.fiber->getState() != Fiber::TERM      
                        && ft.fiber->getState() != Fiber::EXCEPT){  // 没有被终止和移除，则设置挂起
                ft.fiber->m_state = Fiber::HOLD;
            }
            ft.reset();    // 把协程任务以挂起状态返回给队列
        } else if(ft.cb){        // 任务未携带协程，但有执行函数
            if(cb_fiber){        // 如果该线程的执行协程指针cb_fiber有指向
                cb_fiber->reset(ft.cb);    // 则cb_fiber指向新任务
            } else{        // 否则，新创建协程
                cb_fiber.reset(new Fiber(ft.cb));
            }
            ft.reset();
            cb_fiber->swapIn();
            --m_activeThreadCount;
            // SYLAR_LOG_DEBUG(g_logger) << "cb_fiber.state:" << cb_fiber->getState();
            if(cb_fiber->getState() == Fiber::READY){
                schedule(cb_fiber);
                cb_fiber.reset();   // 智能指针reset
            } else if(cb_fiber->getState() == Fiber::EXCEPT
                        || cb_fiber->getState() == Fiber::TERM){
                cb_fiber->reset(nullptr);                
            } else {// if(cb_fiber->getState() != Fiber::TERM){
                cb_fiber->m_state = Fiber::HOLD;
                cb_fiber.reset();
            }
        } else {
            if(is_active){
                --m_activeThreadCount;
                continue;
            }
            if(idle_fiber->getState() == Fiber::TERM){
                SYLAR_LOG_INFO(g_logger) << "idle fiber term";
                break;
            }
            ++m_idleThreadCount;
            idle_fiber->swapIn();
            --m_idleThreadCount;
            if(idle_fiber->getState() != Fiber::TERM
                    && idle_fiber->getState() != Fiber::EXCEPT){
                idle_fiber->m_state = Fiber::HOLD;
            }
        }

    }
}

void Scheduler::tickle(){
    SYLAR_LOG_INFO(g_logger) << "tikle";
}

bool Scheduler::stopping(){
    return m_autoStop && m_stopping
            && m_fibers.empty() && m_activeThreadCount == 0;
}

void Scheduler::idle(){
    SYLAR_LOG_INFO(g_logger) << "idle";
    while(!stopping()){
        sherry::Fiber::YieldToHold();
    }
}

}