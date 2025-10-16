#include "ota_message.h"
#include "log.h"

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

namespace sherry{

OTAMessage::OTAMessage(const std::string& query_str, const std::string& download_str)
    :m_query_msg(query_str)
    ,m_download_msg(download_str){
}

void OTAMessage::set_queryMsg(const std::string& query_str){
    MutexType::Lock lock(m_mutex);
    m_query_msg = std::move(query_str);
}

void OTAMessage::set_downloadMsg(const std::string& download_str){
    MutexType::Lock lock(m_mutex);
    m_download_msg = std::move(download_str);
}

std::pair<std::string, std::string> OTAMessage::get_message(){
    MutexType::Lock lock(m_mutex);
    return {m_query_msg, m_download_msg};
}

std::string OTAMessage::get_queryMsg(){
    MutexType::Lock lock(m_mutex);
    return m_query_msg;
}
std::string OTAMessage::get_downloadMsg(){
    MutexType::Lock lock(m_mutex);
    return m_download_msg;
}



}