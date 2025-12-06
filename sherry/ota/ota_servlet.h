#ifndef __SHERRY_OTA_SERVLET_
#define __SHERRY_OTA_SERVLET_

#include "../ota_client_callback.h"

#include <memory>

namespace sherry{

class OTAServletManager{
public:
    typedef std::shared_ptr<OTAServletManager> ptr;
    OTAServletManager(const std::string& ota_redis_pool_name
                      , OTAClientCallbackManager::ptr cb_mgr);
    void add_servlet(const std::string& topic
                     , const std::string& redis_key
                     , int expire_seconds = 0);
private:
    std::string m_redis_pool_name;
    OTAClientCallbackManager::ptr m_cb_mgr;
};


} // namespace sherry


#endif