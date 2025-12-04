#ifndef __SHERRY_OTA_HASH_
#define __SHERRY_OTA_HASH_

#include <string>
#include <sstream>

#define OTA_TOPIC_PREX "ota/"

namespace sherry{

inline std::string ota_get_query_topic(uint32_t device_type
                                       , const std::string& action){
    std::stringstream ss;
    ss << OTA_TOPIC_PREX << device_type << "/" << action << "/query";
    return ss.str();
}

inline std::string ota_get_response_topic(uint32_t device_type
                                          , const std::string& action){
    std::stringstream ss;
    ss << OTA_TOPIC_PREX << device_type << "/" << action << "/response";
    return ss.str();
}

inline std::string ota_get_query_download_topic(uint32_t device_type
                                                , const std::string& name){
    std::stringstream ss;
    ss << OTA_TOPIC_PREX << device_type << "/" << name << "/query_download";
    return ss.str();
}

} // namespace sherry
#endif