#ifndef __SHERRY_MQTT_CALLBACK_MANAGER_H__
#define __SHERRY_MQTT_CALLBACK_MANAGER_H__

#include <string>
#include <unordered_map>
#include <functional>

#include "sherry.h"

namespace sherry{

class OTAClientCallbackManager{
public:
    typedef std::function<void(const std::string& topic, const std::string& payload)> CallbackFunc;
    typedef std::shared_ptr<OTAClientCallbackManager> ptr;
    typedef RWMutex RWMutexType;

    OTAClientCallbackManager()
        :m_default_callback(nullptr){
            m_callbacks.clear();
    }

    void set_default_callback(CallbackFunc& v) { m_default_callback = std::move(v);}
    void regist_callback(const std::string& topic, CallbackFunc callback);
    void on_message(const std::string& topic, const std::string& payload);
private:
    std::unordered_map<std::string, CallbackFunc> m_callbacks;
    CallbackFunc m_default_callback;
    RWMutexType m_mutex;

};

}

#endif