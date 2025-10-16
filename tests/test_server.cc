#include <cstring>
#include <memory>
#include "../sherry/sherry.h"
#include "../sherry/socket.h"
#include "../sherry/address.h"
#include "../sherry/iomanager.h"

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

void test_socket() {
    // struct sockaddr_in sa;
    const char * addr_str = "10.0.16.11";
    // sa.sin_family = AF_INET;
    // sa.sin_port = htons(80);
    // if (inet_pton(AF_INET, addr_str, &sa.sin_addr) <= 0) {
    //     SYLAR_LOG_ERROR(g_logger) << "inet_pton error";
    //     return;
    // }

    sherry::IPAddress::ptr addr = sherry::IPAddress::Create(addr_str, 80);

    SYLAR_LOG_INFO(g_logger) << "addr Address: " << addr->getAddr();
    SYLAR_LOG_INFO(g_logger) << "addr Family:" << addr->getFamily();
    SYLAR_LOG_INFO(g_logger) << "addr Port:" << addr->getPort();


    sherry::Socket::ptr sock = sherry::Socket::CreateTCPSocket();
    SYLAR_LOG_DEBUG(g_logger) << "sock fd=" << sock->getSocket();
    if (!sock->bind(addr)) {
        SYLAR_LOG_ERROR(g_logger) << "bind error";
        sock->close();
        return;
    }
    if (!sock->listen()) {
        SYLAR_LOG_ERROR(g_logger) << "listen error";
        sock->close();
        return;
    }
    sherry::Socket::ptr sock_accept = sock->accept();
    if (!sock_accept) {
        SYLAR_LOG_ERROR(g_logger) << "accept error";
        sock->close();
        return;
    }
    char buffer[128];
    int i = 0;
    while (++i <= 5) {
        memset(buffer, 0, sizeof(buffer));
        sock_accept->recv(buffer, sizeof(buffer));
        
        SYLAR_LOG_INFO(g_logger) << "server recv: " << buffer;
        SYLAR_LOG_INFO(g_logger) << "swapin";
        
    }
    SYLAR_LOG_DEBUG(g_logger) << "skip out while";
    SYLAR_LOG_INFO(g_logger) << "end";

    sock_accept->close();
    sock->close();
}

int main() {
    sherry::IOManager::ptr iom = std::make_shared<sherry::IOManager>();
    iom->schedule(&test_socket);
    return 0;
}