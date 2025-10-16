#include "sherry/sherry.h"
#include <assert.h>

sherry::Logger::ptr g_looger = SYLAR_LOG_ROOT();

void test_assert(){
    SYLAR_LOG_INFO(g_looger) << sherry::BacktraceToString(10);
    //SYLAR_ASSERT(false);
    SYLAR_ASSERT2(0 == 1, "abcdef");
}

int main(int argc, char ** argv){
    test_assert();
    return 0;
}