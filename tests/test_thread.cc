#include "sherry/sherry.h"
#include <vector>
#include <unistd.h>
sherry::Logger::ptr g_looger = SYLAR_LOG_ROOT();
// sherry::RWMutex s_mutex;
int count = 0;
sherry::Mutex s_mutex;

void fun1(){
    SYLAR_LOG_INFO(g_looger) << "name: " << sherry::Thread::GetName()
                             << " this.name: " << sherry::Thread::GetThis()->getName()
                             << " id: " << sherry::GetThreadId()
                             << " this.id: " << sherry::Thread::GetThis()->getId();
    for(int i = 0;i < 100000;++i){
        //sherry::RWMutex::WriteLock lock(s_mutex);
        sherry::Mutex::Lock lock(s_mutex);
        ++count;
    }
}

void fun2(){
    while(true){
        SYLAR_LOG_INFO(g_looger) << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    }
}

void fun3(){
    while(true){
        SYLAR_LOG_INFO(g_looger) << "==========================================";
    }
}

int main(int argc, char ** argv){
    SYLAR_LOG_INFO(g_looger) << "thread test begin";
    YAML::Node root = YAML::LoadFile("/home/xxl/sherry/bin/conf/log2.yml");
    sherry::Config::LoadFromYaml(root);

    std::vector<sherry::Thread::ptr> thrs;
    for(int i = 0;i < 2;++i){
        sherry::Thread::ptr thr(new sherry::Thread(&fun2, "name_" + std::to_string(i * 2)));
        sherry::Thread::ptr thr2(new sherry::Thread(&fun3, "name_" + std::to_string(i * 2)));
        thrs.push_back(thr);
        thrs.push_back(thr2);
    }

    for(size_t i = 0;i < thrs.size();++i){
        thrs[i]->join();
    }

    

    SYLAR_LOG_INFO(g_looger) << "thread test end";
    SYLAR_LOG_INFO(g_looger) << "count=" << count;

    return 0;
}