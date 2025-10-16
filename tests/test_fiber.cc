#include "sherry/sherry.h"
#include <vector>
sherry::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void run_in_fiber(){
    SYLAR_LOG_INFO(g_logger) << "run_in_fiber begin";
    sherry::Fiber::YieldToHold();
    SYLAR_LOG_INFO(g_logger) << "run_in_fiber end";
    sherry::Fiber::YieldToHold();

}

void test_fiber(){
    SYLAR_LOG_INFO(g_logger) << "main begin -1";
    {
        sherry::Fiber::GetThis();
        SYLAR_LOG_INFO(g_logger)  << "main begin";
        sherry::Fiber::ptr fiber(new sherry::Fiber(run_in_fiber));
        
        fiber->call();
        SYLAR_LOG_INFO(g_logger) << "main after swapIn";
        fiber->call();

        SYLAR_LOG_INFO(g_logger) << "main after end";
        fiber->call();
    }
    SYLAR_LOG_INFO(g_logger) << "main after end2";
}

int main(int argc, char ** argv){
    sherry::Thread::SetName("main");
    
    std::vector<sherry::Thread::ptr> thrs;
    for(int i = 0;i < 1;++i){
        thrs.push_back(sherry::Thread::ptr(new sherry::Thread(&test_fiber, "name_" + std::to_string(i))));
    }

    for(auto i : thrs){
        i->join();
    }

    return 0;
}