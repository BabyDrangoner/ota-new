#include "redis_util.h"
#include "redis.h"
#include "sherry/log.h"

#define TAG "[REDIS_UTIL]"


namespace sherry{
static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

int redis_set_key_value(const std::string& pool_name, const std::string& key, const std::string& value, int expire_seconds){
    if(expire_seconds > 0){
        // SET key value EX seconds
        auto reply = RedisUtil::Cmd(pool_name, "SET %s %s EX %d", key.c_str(), value.c_str(), expire_seconds);
        if(!reply || reply->type == REDIS_REPLY_ERROR){
            SYLAR_LOG_ERROR(g_logger) << TAG << " redis set key with expire failed: " << key;
            return 0;
        }
    } else {
        // SET key value
        auto reply = RedisUtil::Cmd(pool_name, "SET %s %s", key.c_str(), value.c_str());
        if(!reply || reply->type == REDIS_REPLY_ERROR){
            SYLAR_LOG_ERROR(g_logger) << TAG << " redis set key failed: " << key;
            return 0;
        }
    }
    
    SYLAR_LOG_DEBUG(g_logger) << TAG << " redis set key success: " << key;
    return 1;
}

int redis_reset_value(const std::string& pool_name, const std::string& key, const std::string& value, int expire_seconds){
    // 先删除旧值
    if(redis_del_key(pool_name, key) == 0){
        SYLAR_LOG_WARN(g_logger) << TAG << " redis del key failed (may not exist): " << key;
    }
    
    // 设置新值
    return redis_set_key_value(pool_name, key, value, expire_seconds);
}

int redis_del_key(const std::string& pool_name, const std::string& key){
    auto reply = RedisUtil::Cmd(pool_name, "DEL %s", key.c_str());
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

int redis_get_key_value(const std::string& pool_name, const std::string& key, std::string& value){
    auto reply = RedisUtil::Cmd(pool_name, "GET %s", key.c_str());
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

int redis_key_exists(const std::string& pool_name, const std::string& key){
    auto reply = RedisUtil::Cmd(pool_name, "EXISTS %s", key.c_str());
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

int redis_set_expire(const std::string& pool_name, const std::string& key, int expire_seconds){
    auto reply = RedisUtil::Cmd(pool_name, "EXPIRE %s %d", key.c_str(), expire_seconds);
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

// ret: -1 quert null, 0 query error, 1 quert successfully
int redis_query_by_http(const std::string& pool_name, const std::string& key
                        , http::HttpResponse::ptr rsp){
    auto reply_query = RedisUtil::Cmd(pool_name, "GET %s", key.c_str());
    if(!reply_query){
        SYLAR_LOG_ERROR(g_logger) << TAG
            << " redis query by http error, reply is null, key "
            << key;
        rsp->setStatus(http::HttpStatus::INTERNAL_SERVER_ERROR);
        return 0; 
    } 

    int ret = 1;
    switch (reply_query->type){
        case REDIS_REPLY_STRING:
            rsp->setStatus(http::HttpStatus::OK);
            rsp->setBody(reply_query->str);
            break;
        case REDIS_REPLY_NIL:
            rsp->setStatus(http::HttpStatus::OK);
            ret = -1;
            break;
        case REDIS_REPLY_ERROR:
            SYLAR_LOG_ERROR(g_logger) << TAG
                << " redis query by http error, reply is null, key "
                << key;
            rsp->setStatus(http::HttpStatus::INTERNAL_SERVER_ERROR);
            ret = 0;
            break;
        default:
            ret = 0;
            break;
    }

    return ret;
}

int redis_push_message_queue_by_http(const std::string& pool_name, const std::string& key
                                     , const std::string& value, http::HttpResponse::ptr rsp){
    auto reply_push_message_queue = RedisUtil::Cmd(pool_name,
                                                   "LPUSH %s %s", key.c_str(), value.c_str());
    if(!reply_push_message_queue){
        SYLAR_LOG_ERROR(g_logger) << TAG
            << " redis push message queue by http error, reply is null, key "
            << key;
        rsp->setStatus(http::HttpStatus::INTERNAL_SERVER_ERROR);
        return 0;
    }

    int ret = 1;
    switch (reply_push_message_queue->type) {
        case REDIS_REPLY_INTEGER:
            // LPUSH 成功，返回 list 新长度
            rsp->setStatus(http::HttpStatus::OK);
            rsp->setBody("task submits successfully.");
            ret = 1;
            break;
        case REDIS_REPLY_ERROR:
            SYLAR_LOG_ERROR(g_logger) << TAG
                << " redis push message queue by http error, reply type is ERROR, key "
                << key;
            rsp->setStatus(http::HttpStatus::INTERNAL_SERVER_ERROR);
            ret = 0;
            break;
        default:
            SYLAR_LOG_ERROR(g_logger) << TAG
                << " redis push message queue by http error, unexpected reply type="
                << reply_push_message_queue->type << ", key " << key;
            rsp->setStatus(http::HttpStatus::INTERNAL_SERVER_ERROR);
            ret = 0;
            break;
    }

    return ret;
}

} // namespace sherry