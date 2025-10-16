#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int main() {
    // 1.创建套接字
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        exit(-1);
    }

    // 2. 连接服务器
    struct sockaddr_in serveraddr;
    serveraddr.sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.16.11", &serveraddr.sin_addr);
    serveraddr.sin_port = htons(80);

    int ret = connect(fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
    if (ret == -1) {
        perror("connect");
        close(fd);  // 关闭套接字
        exit(-1);
    }

    // 3.通信
    char recvBuf[1024] = {0};
    char *data = "hello, i am client";
    ssize_t sent_bytes;

    while (1) {
        sent_bytes = write(fd, data, strlen(data));
        if (sent_bytes == -1) {
            perror("write");
            break;
        } else if (sent_bytes != strlen(data)) {
            printf("Partial data sent\n");
        }

        sleep(1);
        printf("write end\n");

        // 获取服务器的数据
        // int len = read(fd, recvBuf, sizeof(recvBuf));
        // if (len == -1) {
        //     perror("read");
        //     break;
        // } else if (len > 0) {
        //     printf("recv server data : %s\n", recvBuf);
        // } else if (len == 0) {
        //     // 表示服务端断开连接
        //     printf("server closed.....");
        //     break;
        // }
        // memset(recvBuf, 0, sizeof(recvBuf));  // 清空缓冲区
    }

    // 4.关闭连接
    close(fd);
    return 0;
}