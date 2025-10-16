#include "../sherry/tcp_server.h"
#include "../sherry/iomanager.h"
#include "../sherry/log.h"

#include <thread>
sherry::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void run() {
    auto addr = sherry::Address::LookupAny("0.0.0.0:8033");
    // auto addr2 = sherry::UnixAddress::ptr(new sherry::UnixAddress("/tmp/unix_addr"));
    std::vector<sherry::Address::ptr> addrs;
    addrs.push_back(addr);
    // addrs.push_back(addr2);

    sherry::TcpServer::ptr tcp_server(new sherry::TcpServer);
    std::vector<sherry::Address::ptr> fails;
    while(!tcp_server->bind(addrs, fails)) {
        sleep(2);
    }
    tcp_server->start();
    
}


int main(int argc, char** argv) {
    sherry::IOManager iom(2);
    iom.schedule(run);

    return 0;
}