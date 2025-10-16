#include "../sherry/sherry.h"
#include "../sherry/myfiber.h"

sherry::Logger::ptr g_logger = SYLAR_LOG_ROOT();
void run_in_fiber(){
    SYLAR_LOG_INFO(g_logger) << "swapin success";
    sherry::MyFiber::ptr cur = sherry::MyFiber::GetThis();
    cur->swapout();
}

void test_my_fiber(){
    std::function<void()> cb = run_in_fiber;
    sherry::MyFiber::ptr fiber(new sherry::MyFiber(cb));
    sherry::MyFiber::GetThis();
    fiber->swapin();
    SYLAR_LOG_INFO(g_logger) << "swapout success";
}

int main(){
    sherry::Thread thread(test_my_fiber, "test_my_fiber");
    thread.join();
    return 0;
}