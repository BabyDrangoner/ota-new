#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket create failed");
        return -1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);  // 你的 HttpServer 监听端口
    server_addr.sin_addr.s_addr = inet_addr("10.0.16.11"); // 服务器内网 IP

    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect failed");
        return -1;
    }

    // 发送 HTTP GET 请求
    std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: 81.68.92.10\r\n"
        "Connection: close\r\n"
        "\r\n";

    send(sock, request.c_str(), request.size(), 0);

    // 接收响应
    char buffer[4096];
    ssize_t len;
    while ((len = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[len] = '\0';  // 加字符串终止符
        std::cout << buffer;
    }

    close(sock);
    return 0;
}
