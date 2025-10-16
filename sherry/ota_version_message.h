#ifndef _OTA_VERSION_MESSAGE_H__
#define _OTA_ERSION_MESSAGE_H__

#include <iostream>
#include "sherry.h"

namespace sherry{

class OTAVersionMessage{
public:
    typedef std::shared_ptr<OTAVersionMessage> ptr;
    typedef RWMutex RWMutexType;

    OTAVersionMessage(std::string name
                     ,std::string version
                     ,std::string time
                     ,std::string filename
                     ,size_t file_size
                     ,std::string url_path
                     ,std::string md5_value
                     ,int lauch_mode
                     ,int upgrade_mode)
        :m_name(name)
        ,m_version(version)
        ,m_time(time)
        ,m_filename(filename)
        ,m_file_size(file_size)
        ,m_url_path(url_path)
        ,m_md5_value(md5_value)
        ,m_launch_mode(lauch_mode)
        ,m_upgrade_mode(upgrade_mode){

    }
    
    std::string get_name() const { return m_name;}
    void set_name(const std::string& v) {m_name = v;}

    std::string get_version() const { return m_version;}
    void set_version(const std::string& v) { m_version = v;}
    
    std::string get_time() const { return m_time;}
    void set_time(const std::string& v) { m_time = v;}

    std::string get_filename() const { return m_filename;}
    void set_filename(const std::string& v) { m_filename = v;}

    // std::string get_upgrade_time() const { return m_upgrade_time;}
    // void set_filename(const std::string& v) { m_filename = v;}


private:
    RWMutexType m_mutex;

    std::string m_name;
    std::string m_version;
    std::string m_time;
    std::string m_filename;
    size_t m_file_size;
    std::string m_url_path;
    std::string m_md5_value;
    int m_launch_mode;
    int m_upgrade_mode;
};

}
#endif