#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>

int main() {
    const char* server_ip = "127.0.0.1";  // 服务器IP
    const int server_port = 8020;         // 服务器端口

    // 1. 创建 socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        perror("socket");
        return 1;
    }

    // 2. 设置服务器地址结构体
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // 3. 连接服务器
    if(connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    std::cout << "Connected to server " << server_ip << ":" << server_port << std::endl;

    // 4. 不发送数据，只挂起等待（比如可以观察连接是否维持）
    while(true) {
        pause();  // 让进程挂起，直到收到信号
    }

    // close(sockfd); // 永远不会到这一步
    return 0;
}
