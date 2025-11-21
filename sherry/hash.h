#ifndef _SHERRY_HASH_
#define _SHERRY_HASH_

#include <string>
#include <sstream>
#include <chrono>

#include "util.h"

namespace sherry{

class OTAHash{
public:

    static std::string get_device_group_hash(uint16_t group_id){
        std::stringstream ss;
        ss << ota_redis_key_prefix << group_id;
        return ss.str();
    }

    static std::string get_device_id_hash(uint16_t group_id, uint32_t device_id){
        std::stringstream ss;
        ss << ota_redis_key_prefix << group_id << ":" << device_id;
        return ss.str();
    }

    static std::string get_device_group_id_timestamp_hash(uint16_t group_id, uint32_t device_id){
        std::stringstream ss;
        ss << ota_redis_key_prefix << group_id << ":" << device_id << ":" << GetCurrentUS();
        return ss.str();
    }

    static std::string get_device_group_mudule_hash(uint16_t group_id, const std::string& name){
        std::stringstream ss;
        ss << ota_redis_key_prefix << group_id << ":" << name;
        return ss.str();
    }

    static std::string get_device_mudule_hash(uint16_t group_id, uint32_t device_id, const std::string& name){
        std::stringstream ss;
        ss << ota_redis_key_prefix << group_id << ":" << device_id << ":" << name;
        return ss.str();
    }

    static std::string get_device_mudule_flag_hash(uint16_t group_id, uint32_t device_id, const std::string& name){
        return get_device_group_mudule_hash(group_id, device_id, name) + ":flag";
    }

private:
    static constexpr const char* ota_redis_key_prefix = "ota:device:";
};

}  // namespace sherry
#endif