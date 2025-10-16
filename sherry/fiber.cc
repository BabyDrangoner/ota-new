#include "fiber.h"
#include "config.h"
#include "macro.h"
#include "log.h"
#include "scheduler.h"
#include <atomic>

namespace sherry{

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static std::atomic<uint64_t> s_fiber_id {0};
static std::atomic<uint64_t> s_fiber_count {0};

static thread_local Fiber * t_fiber = nullptr;
static thread_local Fiber::ptr t_threadFiber = nullptr;

static ConfigVar<uint32_t>::ptr g_fiber_stack_size = 
        Config::Lookup<uint32_t>("fiber.stack_size", 1024 * 1024, "fiber stack size");

class MallocStackAllocator{
public:
    static void * Alloc(size_t size){
        return malloc(size);
    }

    static void Dealloc(void * vp, size_t size){
        return free(vp);
    }
};

using StackAllocator = MallocStackAllocator;

uint64_t Fiber::GetFiberId(){
    if(t_fiber){
        return t_fiber->getId();
    }
    return 0;
}


Fiber::Fiber(){
    m_state = EXEC;
    SetThis(this);

    if(getcontext(&m_ctx)){
        SYLAR_ASSERT2(false, "getcontext");
    }

    ++s_fiber_count;

    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber";
}

Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool use_caller)  //
        :m_id(++s_fiber_id)
        ,m_cb(cb){
        ++s_fiber_count;

        // 申请协程栈内存
        m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();
        m_stack = StackAllocator::Alloc(m_stacksize);
        
        // 保存上下文
        if(getcontext(& m_ctx)){
            SYLAR_ASSERT2(false, "getcontext");
        }
        m_ctx.uc_link = nullptr;
        m_ctx.uc_stack.ss_sp = m_stack;
        m_ctx.uc_stack.ss_size = m_stacksize;

        // 设置协程执行函数（主函数，来调用m_cb）
        if(!use_caller){
            makecontext(&m_ctx, &Fiber::MainFunc, 0);
        } else {
            makecontext(&m_ctx, &Fiber::CallerMainFunc, 0);
        }

        SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber id=" << m_id;
}

Fiber::~Fiber(){
    --s_fiber_count;
    // 释放申请的协程栈内存
    if(m_stack){
        SYLAR_ASSERT(m_state == TERM
                    || m_state == EXCEPT
                    || m_state == INIT);
        StackAllocator::Dealloc(m_stack, m_stacksize);
    } else{
        SYLAR_ASSERT(!m_cb);
        SYLAR_ASSERT(m_state == EXEC);

        Fiber * cur = t_fiber;
        if(cur == this){
            SetThis(nullptr);
        }
    }

    SYLAR_LOG_DEBUG(g_logger) << "Fiber::~Fiber id=" << m_id;

}

// 重置协程函数，并重置状态
void Fiber::reset(std::function<void()> cb){
    SYLAR_ASSERT(m_stack);
    SYLAR_ASSERT(m_state == TERM || m_state == INIT || m_state == EXCEPT);
    
    // 更新回调函数
    m_cb = cb;

    // 保存上下文
    if(getcontext(&m_ctx)){
        SYLAR_ASSERT2(false, "getcontext");
    }
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    // 设置主函数
    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    
    // 设置协程状态为INIT
    m_state = INIT;
}

void Fiber::call(){ // 
    SetThis(this);
    m_state = EXEC;
    if(swapcontext(&t_threadFiber->m_ctx, &m_ctx)){
        SYLAR_ASSERT2(false, "getcontext");
    }
}

void Fiber::back(){
    SetThis(t_threadFiber.get());
    if(swapcontext(&m_ctx, &t_threadFiber->m_ctx)){
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

// 切换到当前协程执行
void Fiber::swapIn(){
    // 设置当前协程为当前线程正在运行的协程
    SetThis(this);

    SYLAR_ASSERT(m_state != EXEC);
    
    // 更新协程状态 
    m_state = EXEC;

    // 执行主协程（注意主函数是m_ctx中的，在构造函数中设置，第一个参数只是切回来时的上下文)
    if(swapcontext(&Scheduler::GetMainFiber()->m_ctx, &m_ctx)){
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

// 切换到后台执行
void Fiber::swapOut(){ // 
    // 设置主协程为当前线程的协程
    SetThis(Scheduler::GetMainFiber());

    // 切换上下文，并把当前上下文保存到原协程的ctx
    if(swapcontext(&m_ctx, &Scheduler::GetMainFiber()->m_ctx)){
        SYLAR_ASSERT2(false, "swapcontext");
    }
   
}

// 设置当前协程,将t_fiber设置为当前需要切入的协程类实例
void Fiber::SetThis(Fiber * f){ // 
    t_fiber = f;
}

// 返回当前协程
Fiber::ptr Fiber::GetThis(){ // 
    if(t_fiber){
        return t_fiber->shared_from_this();
    }
    Fiber::ptr main_fiber(new Fiber);
    SYLAR_ASSERT(t_fiber == main_fiber.get());
    t_threadFiber = main_fiber;
    return t_fiber->shared_from_this();
}

// 协程切换到后台，并且设置为ready状态
void Fiber::YieldToReady(){ 
    // 设置当前协程状态为READY
    Fiber::ptr cur = GetThis();
    cur->m_state = READY;

    // 退出当前协程
    cur->swapOut();
}

// 协程切换到后台，并且设置为Hold状态
void Fiber::YieldToHold(){
    // 设置当前协程状态为HOLD
    Fiber::ptr cur = GetThis();
    cur->m_state = HOLD;

    // 退出当前协程
    cur->swapOut();

}
// 总协程数
uint64_t Fiber::TotalFibers(){ //
    return s_fiber_count;
}

uint64_t Fiber::TotalUsedFibers(){
    return s_fiber_id;
}


void Fiber::MainFunc(){ 
    // 获取当前线程运行的协程实例
    Fiber::ptr cur = GetThis();
    SYLAR_LOG_DEBUG(g_logger) << "fiber mainfunc " << "fiber=" << sherry::GetFiberId();
    SYLAR_ASSERT(cur);

    try{
        // 执行协程的回调函数 
        // -----> 设置协程回调为nullptr 
        // -----> 设置协程状态为TERM
    
        cur->m_cb();
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    } catch(std::exception & ex){
        // 处理异常，设置协程状态为EXCEPT
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except: " << ex.what()
                            << " fiber_id=" << cur->getId()
                            << std::endl
                            << sherry::BacktraceToString();
    } catch(...){
        // 处理异常，设置协程状态为EXCEPT
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except: "
                            << " fiber_id=" << cur->getId()
                            << std::endl
                            << sherry::BacktraceToString();
    }

    // 协程重置并退出
    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->swapOut();

    SYLAR_ASSERT2(false, "never reach fiber_id=" + std::to_string(raw_ptr->getId()));
    
}

void Fiber::CallerMainFunc(){
    Fiber::ptr cur = GetThis();
    
    SYLAR_ASSERT(cur);
    try{
        cur->m_cb();
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    } catch(std::exception & ex){
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except: " << ex.what()
                            << " fiber_id=" << cur->getId()
                            << std::endl
                            << sherry::BacktraceToString();
    } catch(...){
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except: "
                            << " fiber_id=" << cur->getId()
                            << std::endl
                            << sherry::BacktraceToString();
    }

    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->back();

    SYLAR_ASSERT2(false, "never reach fiber_id=" + std::to_string(raw_ptr->getId()));
}


}