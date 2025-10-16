#include "../sherry/sherry.h"
#include "../sherry/iomanager.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <sys/epoll.h>

sherry::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int sock = 0;

void test_fiber(){
    SYLAR_LOG_INFO(g_logger) << "test_fiber sock=" << sock;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(sock, F_SETFL, O_NONBLOCK);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "10.192.14.236", &addr.sin_addr.s_addr);
    if(!connect(sock, (const sockaddr *)&addr, sizeof(addr))){
    } else if(errno == EINPROGRESS){
        SYLAR_LOG_INFO(g_logger) << "add event errno=" << errno << " " << strerror(errno);
        sherry::IOManager::GetThis()->addEvent(sock, sherry::IOManager::READ, [](){
            SYLAR_LOG_INFO(g_logger) << "read callback";
            // close(sock);
        });
        sherry::IOManager::GetThis()->addEvent(sock, sherry::IOManager::WRITE, [](){
            SYLAR_LOG_INFO(g_logger) << "write callback";
            sherry::IOManager::GetThis()->cancelEvent(sock, sherry::IOManager::READ);
            close(sock);
        });
    } else{
        SYLAR_LOG_INFO(g_logger) << "else " << errno << " " << strerror(errno); 
    }

    // iom.addEvent(sock, sherry::IOManager::READ, [](){
    //     SYLAR_LOG_INFO(g_logger) << "connected";
    // });

    // iom.addEvent(sock, sherry::IOManager::WRITE, [](){
    //     SYLAR_LOG_INFO(g_logger) << "connected";
    // });
}

void test1(){
    sherry::IOManager iom(2, false);
    iom.schedule(&test_fiber);
}

sherry::Timer::ptr s_timer;
void test_timer(){
    sherry::IOManager iom(2);
    s_timer = iom.addTimer(1000, [](){
        static int i = 0;
        SYLAR_LOG_INFO(g_logger) << "hello timer i=" << i; 
        if(++i == 3){
            s_timer->reset(2000, true);
            // s_timer->cancel();
        }
    }, true);
}

int main(){
    //test1();
    test_timer();
    return 0;
}