在 address.cc 文件中，`addrinfo` 是一个结构体类型，用于存储地址信息。它通常用于网络编程中，特别是在使用 `getaddrinfo` 函数时。`addrinfo` 结构体定义在 `<netdb.h>` 头文件中，包含以下成员：

```cpp
struct addrinfo {
    int              ai_flags;      // 输入标志
    int              ai_family;     // 地址族（例如 AF_INET, AF_INET6）
    int              ai_socktype;   // 套接字类型（例如 SOCK_STREAM, SOCK_DGRAM）
    int              ai_protocol;   // 协议类型（例如 IPPROTO_TCP, IPPROTO_UDP）
    socklen_t        ai_addrlen;    // 地址长度
    struct sockaddr *ai_addr;       // 套接字地址
    char            *ai_canonname;  // 规范名称
    struct addrinfo *ai_next;       // 指向下一个 addrinfo 结构的指针
};
```

