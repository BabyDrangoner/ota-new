#include "myfiber.h"
#include "sherry.h"
#include <atomic>

namespace sherry{

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static thread_local MyFiber::ptr t_threadFiber = nullptr;
static thread_local MyFiber* t_fiber = nullptr;

static std::atomic<uint64_t> s_fiber_id {0};
static std::atomic<uint64_t> s_fiber_count {0};

static ConfigVar<uint32_t>::ptr g_fiber_stack_size = 
        Config::Lookup<uint32_t>("fiber.stack_size", 1024 * 1024, "fiber stack size");

void* MyFiber::allocateStack(size_t size){
    if(size <= 0) return nullptr;
    return malloc(size);
}

void MyFiber::freeStack(void* ptr, size_t size){
    if(ptr == nullptr || size <= 0) return;
    free(ptr);
}


uint64_t MyFiber::GetFiberId(){
    MyFiber::ptr cur = GetThis();
    if(cur){
        return cur->m_id;
    }
    return -1;
}


MyFiber::MyFiber(std::function<void()> cb, size_t stack_size)
    :m_cb(std::move(cb))
    ,m_stackSize(stack_size ? stack_size : g_fiber_stack_size->getValue())
    ,m_state(INIT)
    ,m_id(++s_fiber_id){
    
    m_stack = allocateStack(stack_size);
    getcontext(&m_ctx);
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stackSize;
    m_ctx.uc_link = nullptr;

    makecontext(&m_ctx, &MyFiber::MainFunc, 0);
    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber id=" << m_id;
    ++s_fiber_count;
}

MyFiber::MyFiber(){
    m_state = EXEC;
    m_cb = nullptr;
    m_stack = nullptr;

    if(getcontext(&m_ctx)){
        SYLAR_ASSERT2(false, "getcontext");
    }
    
    SetThis(this);
    ++s_fiber_count;
}

MyFiber::~MyFiber(){
    if(m_stack){
        freeStack(m_stack, m_stackSize);
        m_cb = nullptr;
        m_stack = nullptr;
        --s_fiber_count;
        SYLAR_ASSERT(m_state == TERM || m_state == EXCEPT || m_state == INIT);
        SYLAR_LOG_DEBUG(g_logger) << "Fiber destroyed: id=" << m_id;
    } else {
        SYLAR_ASSERT(!m_cb);
        SYLAR_ASSERT(m_state == EXEC);

        MyFiber* cur = t_fiber;
        if(cur == this){
            SetThis(nullptr);
        }
    }
}

void MyFiber::reset(std::function<void()> cb){
    SYLAR_ASSERT(m_stack);
    SYLAR_ASSERT(m_state == TERM || m_state == INIT || m_state == EXCEPT);
    m_cb = std::move(cb);

    getcontext(&m_ctx);

    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stackSize;
    m_ctx.uc_link = nullptr;

    makecontext(&m_ctx, &MainFunc, 0);

    m_state = INIT;
}


void MyFiber::MainFunc(){
    MyFiber::ptr cur = GetThis();
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

    cur->swapout();
}

MyFiber::ptr MyFiber::GetThis(){
    if(t_fiber == nullptr){
        MyFiber::ptr main_fiber(new MyFiber);
        t_threadFiber = main_fiber;
        t_fiber = t_threadFiber.get();
    }

    return t_fiber->shared_from_this();
} 

void MyFiber::SetThis(MyFiber* f){
    t_fiber = f;
}

void MyFiber::swapin(){
    SYLAR_ASSERT(m_state == INIT || m_state == READY || m_state == HOLD);
    SetThis(this);
    m_state = EXEC;
    SYLAR_ASSERT(t_threadFiber);
    swapcontext(&t_threadFiber->m_ctx, &m_ctx);

}

void MyFiber::swapout(){
    MyFiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur);
    SYLAR_ASSERT(t_threadFiber);
    swapcontext(&cur->m_ctx, &t_threadFiber->m_ctx);
}

// 协程切换到后台，并且设置为ready状态
void MyFiber::YieldToReady(){
    MyFiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur->m_state == EXEC);
    cur->m_state = READY;
    cur->swapout();
}

// 协程切换到后台，并且设置为Hold状态
void MyFiber::YieldToHold(){
    MyFiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur->m_state == EXEC);
    cur->m_state = HOLD;
    cur->swapout();
}

// 总协程数
uint64_t TotalFibers(){
    return s_fiber_count; 
}


}