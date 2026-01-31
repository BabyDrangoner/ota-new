#ifndef __SHERRY_DEVICE_COMPONENT_H__
#define __SHERRY_DEVICE_COMPONENT_H__

#include <string>
#include <memory>
#include <unordered_map>

#include "sherry/thread.h"

namespace sherry{
namespace device{

#define TAG "[DeviceComponet]"
struct ComponentInfo{
    std::string name;
    std::string version;
    std::string md5_value;
    std::string upgrade_time;
    std::string http_uri;
};

class Component{
public:
    typedef std::shared_ptr<Component> ptr;
    typedef RWMutex RWMutexType;

    Component(const std::string& name
             ,const std::string& version
             ,const std::string& md5_value
             ,const std::string& upgrade_time
             ,const std::string& http_uri)
             :m_info(name, version, md5_value
             ,upgrade_time, http_uri){}
    
    void update(const std::string& version
               ,const std::string& md5_value
               ,const std::string& upgrade_time
               ,const std::string& http_uri);

    const *ComponentInfo get_info();
private:
    RWMutexType m_mutex;
    ComponentInfo m_info;
}

struct MetaData{
    uint32_t device_type;
    size_t component_nums;
    std::unordered_map<std::string, class *Component> components;
    ~MetaData(){
        for(auto it = components.begin();
            it != components.end();++it){
            delete it->second;
            it->second = nullptr;
        }
    }
};

} // namespace device
} // namespace sherry

#endif