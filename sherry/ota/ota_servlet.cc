#include "ota_servlet.h"
#include "../db/redis_util.h"
#include "../log.h"

#define TAG "OTAServlet"

namespace sherry{
static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

OTAServletManager::OTAServletManager(const std::string& ota_redis_pool_name
                                     , OTAClientCallbackManager::ptr cb_mgr)
    :m_redis_pool_name(ota_redis_pool_name)
    ,m_cb_mgr(cb_mgr){
}

void OTAServletManager::add_servlet(const std::string& topic
                                    , const std::string& redis_key
                                    , int expire_seconds){
    m_cb_mgr->regist_callback(topic
                              , [this, redis_key, expire_seconds]
                              (const std::string& topic, const std::string& payload){
        SYLAR_LOG_INFO(g_logger) << TAG
            << "Subscribe topic " << topic
            << " payload " << payload;
        if(redis_safe_set_key_value(m_redis_pool_name, redis_key
                                    , payload, expire_seconds) == 0){
            SYLAR_LOG_ERROR(g_logger) << TAG
                << "Subscribe topic " << topic
                << "set redis " << m_redis_pool_name 
                << " value failed.";
        }
    });
}


}; // namespace sherry