#include "../sherry/socket.h"
#include "../sherry/sherry.h"
#include "../sherry/iomanager.h"

static sherry::Logger::ptr g_logger = SYLAR_LOG_ROOT();;

void test_socket(){
    sherry::IPAddress::ptr addr =sherry::Address::LookupAnyIPAddress("www.baidu.com");
    if(addr){
        SYLAR_LOG_INFO(g_logger) << "get address: " << addr->toString();
    } else {
        SYLAR_LOG_ERROR(g_logger) << "get address fail";
        return;
    }

    sherry::Socket::ptr sock = sherry::Socket::CreateTCP(addr);
    SYLAR_LOG_INFO(g_logger) << "addr=" << addr->toString();
    addr->setPort(80);
    if(!sock->connect(addr)){
        SYLAR_LOG_ERROR(g_logger) << "connect " << addr->toString() << "fail";
        return;
    } else {
        SYLAR_LOG_INFO(g_logger) << "connect " << addr->toString() << " connected";
    }
    SYLAR_LOG_DEBUG(g_logger) << "socket=" << sock->getSocket();
    SYLAR_LOG_DEBUG(g_logger) << "family=" << sock->getFamily();
    SYLAR_LOG_DEBUG(g_logger) << "type=" << sock->getType();
    SYLAR_LOG_DEBUG(g_logger) << "protocol=" << sock->getProtocol();
    SYLAR_LOG_DEBUG(g_logger) << "local_address=" << sock->getLocalAddress()->toString();
    SYLAR_LOG_DEBUG(g_logger) << "remote_address=" << sock->getRemoteAddress()->toString();
    
    const char buff[] = "GET / HTTP/1.0\r\n\r\n";
    int rt = sock->send(buff, sizeof(buff));
    if(rt <= 0){
        SYLAR_LOG_INFO(g_logger) << "send fail rt=" << rt;
        return;
    }

    std::string buffs;
    buffs.resize(4096);
    rt = sock->recv(&buffs[0], buffs.size());

    if(rt <= 0){
        SYLAR_LOG_INFO(g_logger) << "recv fail rt=" << rt;
        return;
    }

    buffs.resize(rt);
    SYLAR_LOG_INFO(g_logger) << buffs;
}

void test_socket2(){
    sherry::IPAddress::ptr addr =sherry::Address::LookupAnyIPAddress("www.baidu.com");
    if(addr){
        SYLAR_LOG_INFO(g_logger) << "get address: " << addr->toString();
    } else {
        SYLAR_LOG_ERROR(g_logger) << "get address fail";
        return;
    }

    sherry::Socket::ptr sock = sherry::Socket::CreateTCP(addr);
    SYLAR_LOG_INFO(g_logger) << "addr=" << addr->toString();
    addr->setPort(80);
    SYLAR_LOG_INFO(g_logger) << "sendtimeout=" << sock->getSendTimeout();
}
int main(int argc, char ** argv){
    sherry::IOManager iom;
    iom.schedule(&test_socket);
    return 0;
}