#include "db/redis_util.h"
#include "db/redis.h"
#include "log.h"

#define TAG "[REDIS_UTIL]"

static sherry::Logger::ptr g_logger = SYLAR_LOG_ROOT("system");

namespace sherry{

int redis_set_key_value(const std::string& key, const std::string& value, int expire_seconds){
    if(expire_seconds > 0){
        // SET key value EX seconds
        auto reply = RedisUtil::Cmd("SET %s %s EX %d", key.c_str(), value.c_str(), expire_seconds);
        if(!reply || reply->type == REDIS_REPLY_ERROR){
            SYLAR_LOG_ERROR(g_logger) << TAG << " redis set key with expire failed: " << key;
            return 0;
        }
    } else {
        // SET key value
        auto reply = RedisUtil::Cmd("SET %s %s", key.c_str(), value.c_str());
        if(!reply || reply->type == REDIS_REPLY_ERROR){
            SYLAR_LOG_ERROR(g_logger) << TAG << " redis set key failed: " << key;
            return 0;
        }
    }
    
    SYLAR_LOG_DEBUG(g_logger) << TAG << " redis set key success: " << key;
    return 1;
}

int redis_reset_value(const std::string& key, const std::string& value, int expire_seconds){
    // 先删除旧值
    if(redis_del_key(key) == 0){
        SYLAR_LOG_WARN(g_logger) << TAG << " redis del key failed (may not exist): " << key;
    }
    
    // 设置新值
    return redis_set_key_value(key, value, expire_seconds);
}

int redis_del_key(const std::string& key){
    auto reply = RedisUtil::Cmd("DEL %s", key.c_str());
    if(!reply || reply->type == REDIS_REPLY_ERROR){
        SYLAR_LOG_ERROR(g_logger) << TAG << " redis del key failed: " << key;
        return 0;
    }
    
    // DEL 命令返回删除的 key 数量
    if(reply->type == REDIS_REPLY_INTEGER && reply->integer > 0){
        SYLAR_LOG_DEBUG(g_logger) << TAG << " redis del key success: " << key;
        return 1;
    }
    
    // key 不存在
    SYLAR_LOG_DEBUG(g_logger) << TAG << " redis key not exists: " << key;
    return 0;
}

int redis_get_key_value(const std::string& key, std::string& value){
    auto reply = RedisUtil::Cmd("GET %s", key.c_str());
    if(!reply){
        SYLAR_LOG_ERROR(g_logger) << TAG << " redis get key failed: " << key;
        return 0;
    }
    
    if(reply->type == REDIS_REPLY_ERROR){
        SYLAR_LOG_ERROR(g_logger) << TAG << " redis get key error: " << key;
        return 0;
    }
    
    if(reply->type == REDIS_REPLY_NIL){
        SYLAR_LOG_DEBUG(g_logger) << TAG << " redis key not exists: " << key;
        return 0;
    }
    
    if(reply->type == REDIS_REPLY_STRING && reply->str){
        value.assign(reply->str, reply->len);
        SYLAR_LOG_DEBUG(g_logger) << TAG << " redis get key success: " << key;
        return 1;
    }
    
    SYLAR_LOG_ERROR(g_logger) << TAG << " redis get key unexpected type: " << key;
    return 0;
}

int redis_key_exists(const std::string& key){
    auto reply = RedisUtil::Cmd("EXISTS %s", key.c_str());
    if(!reply || reply->type == REDIS_REPLY_ERROR){
        SYLAR_LOG_ERROR(g_logger) << TAG << " redis exists check failed: " << key;
        return 0;
    }
    
    // EXISTS 返回存在的 key 数量
    if(reply->type == REDIS_REPLY_INTEGER){
        return reply->integer > 0 ? 1 : 0;
    }
    
    return 0;
}

int redis_set_expire(const std::string& key, int expire_seconds){
    auto reply = RedisUtil::Cmd("EXPIRE %s %d", key.c_str(), expire_seconds);
    if(!reply || reply->type == REDIS_REPLY_ERROR){
        SYLAR_LOG_ERROR(g_logger) << TAG << " redis set expire failed: " << key;
        return 0;
    }
    
    // EXPIRE 返回 1 表示成功，0 表示 key 不存在
    if(reply->type == REDIS_REPLY_INTEGER){
        if(reply->integer == 1){
            SYLAR_LOG_DEBUG(g_logger) << TAG << " redis set expire success: " << key;
            return 1;
        } else {
            SYLAR_LOG_WARN(g_logger) << TAG << " redis key not exists when set expire: " << key;
            return 0;
        }
    }
    
    return 0;
}

} // namespace sherry