#include <ucontext.h>
#include <iostream>
#include <cstdlib>

using namespace std;
size_t stacksize = 1024 * 1024; // 合理增大栈空间

ucontext_t m, t;

void fiber_fun1() {
    cout << "子协程 1-1" << endl;
    swapcontext(&t, &m);

}

void fiber_fun() {
    cout << "子协程 1" << endl;
    // 手动切换回主协程
    makecontext(&t, fiber_fun1, 0); 
    swapcontext(&m, &t); 
    cout << "子协程 2" << endl;
    //swapcontext(&m, &t); 
}



int main() {

    getcontext(&m); 
    // 为子协程分配栈空间
    m.uc_stack.ss_sp = malloc(stacksize); 
    m.uc_stack.ss_size = stacksize;

    t.uc_stack.ss_sp = malloc(stacksize); 
    t.uc_stack.ss_size = stacksize;

    // 修改子协程上下文，使其执行 fiber_fun 函数
    makecontext(&m, fiber_fun, 0); 
    

    // 从主协程切换到子协程
    swapcontext(&t, &m); 

    cout << "返回主协程 1" << endl;

    swapcontext(&t, &m); 

    cout << "返回主协程 2" << endl;


    // 释放子协程栈空间
    free(m.uc_stack.ss_sp); 

    return 0;
}