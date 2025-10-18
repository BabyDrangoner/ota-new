#include <iostream>

#include "sherry/db/redis.h"
#include "sherry/iomanager.h"

void run(){
    // 创建一个 Redis 客户端对象
    sherry::Redis::ptr redis = std::make_shared<sherry::Redis>();
    redis->setPasswd("322322");
    redis->setName("xiaolong.xu");
    // 连接 Redis（可指定超时）
    if (!redis->connect("127.0.0.1", 6379, 2000)) {
        std::cerr << "Redis connect failed!" << std::endl;
        return;
    }
    std::cout << "Connected to Redis successfully!" << std::endl;

    // 执行 SET 命令
    auto reply1 = redis->cmd("SET %s %s", "sherry", "haibala");
    if (reply1 && reply1->type != REDIS_REPLY_ERROR) {
        std::cout << "SET success" << std::endl;
    }

    // 执行 GET 命令
    auto reply2 = redis->cmd("GET %s", "foo");
    if (reply2 && reply2->type == REDIS_REPLY_STRING) {
        std::cout << "GET foo = " << reply2->str << std::endl;
    }

    // 管道示例（pipeline）
    redis->appendCmd("INCR counter");
    redis->appendCmd("INCR counter");
    auto r1 = redis->getReply();
    auto r2 = redis->getReply();
    std::cout << "Counter after 2 increments: " << r2->integer << std::endl;

    std::cout << "over" << std::endl;
}

int main(int argc, char** argv) {
    run();
    return 0;
}
