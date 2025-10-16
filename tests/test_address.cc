#include "../sherry/address.h"
#include "../sherry/log.h"

sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

void test(){
    std::vector<sherry::Address::ptr> addrs;

    SYLAR_LOG_INFO(g_logger) << "begin";
    bool v = sherry::Address::Lookup(addrs, "www.sylar.top");
    SYLAR_LOG_INFO(g_logger) << "end";
    if(!v){
        SYLAR_LOG_ERROR(g_logger) << "Lookup fail";
        return;
    }

    for(size_t i = 0; i < addrs.size(); ++i){
        SYLAR_LOG_INFO(g_logger) << i << " - " << addrs[i]->toString();
    }
}

void test_iface(){
    std::multimap<std::string, std::pair<sherry::Address::ptr, uint32_t> >  results;

    bool v = sherry::Address::GetInterfaceAddresses(results);
    if(!v){
        SYLAR_LOG_ERROR(g_logger) << "GetInterfaceAddress fail";
        return;
    }

    for(auto & i : results){
        SYLAR_LOG_INFO(g_logger) << i.first << " - " << i.second.first->toString() << " - "
            << i.second.second;
    }
}

void test_ipv4(){
    auto addr = sherry::IPAddress::Create("127.0.0.8");
    if(addr){
        SYLAR_LOG_INFO(g_logger) << addr->toString();
    }
}

int main(int argc, char ** argv){
    // test();
    // test_iface();
    test_ipv4();
    return 0;
}