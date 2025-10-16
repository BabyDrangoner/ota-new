#ifndef _SHERRY_MYFIBER_H__
#define _SHERRY_MYFIBER_H__

#include "sherry.h"
#include <ucontext.h>
#include <functional>

namespace sherry{

class MyFiber :  public std::enable_shared_from_this<MyFiber>{
public:
    typedef std::shared_ptr<MyFiber> ptr;

    static void* allocateStack(size_t size);
    static void freeStack(void* ptr, size_t size);
    
    enum State{
        INIT,
        EXEC,
        READY,
        HOLD,
        EXCEPT,
        TERM
    };

    MyFiber(std::function<void()> cb, size_t stack_size = 128 * 1024);
    ~MyFiber();
    
    static MyFiber::ptr GetThis();
    uint64_t GetFiberId();

    void reset(std::function<void()> cb);

    static void SetThis(MyFiber* f);

    static void MainFunc();

    void swapin();
    void swapout();

    // 协程切换到后台，并且设置为ready状态
    static void YieldToReady();
    // 协程切换到后台，并且设置为Hold状态
    static void YieldToHold();
    // 总协程数
    static uint64_t TotalFibers();

    uint64_t getId() const { return m_id;}
    State getState() const { return m_state;}


private:
    MyFiber();
    std::function<void()> m_cb;
    size_t m_stackSize;
    State m_state;
    uint64_t m_id;    

    ucontext_t m_ctx;
    void* m_stack;
};
}

#endif