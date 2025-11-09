#include <iostream>

#include "sherry/db/redis.h"
#include "sherry/iomanager.h"
#include "sherry/config.h"

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
    auto reply2 = redis->cmd("GET %s", "sherry");
    if (reply2 && reply2->type == REDIS_REPLY_STRING) {
        std::cout << "GET foo = " << reply2->str << std::endl;
    }

    // 执行 SET 命令
    auto reply3 = redis->cmd("SET %s %s", "sherry", "ai");
    if (reply3 && reply3->type != REDIS_REPLY_ERROR) {
        std::cout << "SET success" << std::endl;
    }

    // 执行 GET 命令
    auto reply4 = redis->cmd("GET %s", "sherry");
    if (reply4 && reply4->type == REDIS_REPLY_STRING) {
        std::cout << "GET foo = " << reply4->str << std::endl;
    }
    

    std::cout << "over" << std::endl;
}

void test_redisManager(){
    sherry::RedisManager redisManager;

    auto reply = sherry::RedisUtil::Cmd("local", "SET %s %s", "test", "101");

    if(reply){
        std::cout << reply->str << std::endl;
    }

    auto reply1 = sherry::RedisUtil::Cmd("local", "GET %s", "test");
    if(reply1){
        std::cout << reply1->str << std::endl;
    }

    auto reply2 = sherry::RedisUtil::Cmd("local", "SET %s %s", "test", "102");

    if(reply2){
        std::cout << reply2->str << std::endl;
    }

    auto reply3 = sherry::RedisUtil::Cmd("local", "GET %s", "test");
    if(reply3){
        std::cout << reply3->str << std::endl;
    }
}

int main(int argc, char** argv) {
    // run();
    // 1. 加载配置文件
    try {
        YAML::Node config = YAML::LoadFile("./config/ota_system.yaml");
        sherry::Config::LoadFromYaml(config);
    } catch(const std::exception& e) {
        // 配置加载失败，但继续运行（使用默认配置）
    }
    test_redisManager();
    return 0;
}
