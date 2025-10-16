#ifndef _SHERRY_OTA_MESSAGE_H__
#define _SHERRY_OTA_MESSAGE_H__

#include "sherry.h"
#include "../include/json/json.hpp"

namespace sherry{

struct query_message{
    std::string action;
    int no;
    std::string time;
    std::vector<struct result> results;

    struct result{
        std::string name;
        std::string version;
        std::string upgrade_time;
    };

    std::string to_string();
};

class OTAMessage{
public:
    typedef std::shared_ptr<OTAMessage> ptr;
    typedef Mutex MutexType;

    OTAMessage(const std::string& query_str="", const std::string& download_str="");
    
    void set_queryMsg(const std::string& query_str);
    void set_downloadMsg(const std::string& download_str);

    std::pair<std::string, std::string> get_message();
    std::string get_queryMsg();
    std::string get_downloadMsg();

private:
    MutexType m_mutex;
    std::string m_query_msg;
    std::string m_download_msg;
};

}

#endif