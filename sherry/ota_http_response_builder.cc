#include "ota_http_response_builder.h"
#include "log.h"

namespace sherry{

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

OTAResponse::OTAResponse(float http_version)
    :m_http_version(http_version){
}

OTANotifyRes::OTANotifyRes(float http_version)
    :OTAResponse(http_version){
    
}

std::string OTANotifyRes::build_http_response(bool success, nlohmann::json& json_response){
    if(success){
        json_response["status"] = "ok";
    } else {
        json_response["status"] = "error";
    }

    std::string body = json_response.dump();

    std::stringstream ss;
    ss << "HTTP/" << m_http_version;
    if(success){
        ss << " 200 OK\r\n";
    } else {
        ss << " 400 Bad Request\r\n";
    }

    ss << "Content-Type: application/json"
       << "\r\nContent-Length: " << body.size()
       << "\r\nConnection: " << "close\r\n"
       << "\r\n" << body;
    
    return ss.str();
}

OTAStopNotifyRes::OTAStopNotifyRes(float http_version)
    :OTAResponse(http_version){

}

std::string OTAStopNotifyRes::build_http_response(bool success, nlohmann::json& json_response){
    if(success){
        json_response["status"] = "ok";
    } else {
        json_response["status"] = "error";
    }

    std::string body = json_response.dump();

    std::stringstream ss;
    ss << "HTTP/" << m_http_version;
    if(success){
        ss << " 200 OK\r\n";
    } else {
        ss << " 400 Bad Request\r\n";
    }

    ss << "Content-Type: application/json"
       << "\r\nContent-Length: " << body.size()
       << "\r\nConnection: " << "close\r\n"
       << "\r\n" << body;
    
    return ss.str();
}

OTAQueryRes::OTAQueryRes(float http_version)
    :OTAResponse(http_version){
    
}

std::string OTAQueryRes::build_http_response(bool success, nlohmann::json& json_response){
    if(success){
        json_response["status"] = "ok";
    } else {
        json_response["status"] = "error";
    }

    std::string body = json_response.dump();

    std::stringstream ss;
    ss << "HTTP/" << m_http_version;
    if(success){
        ss << " 200 OK\r\n";
    } else {
        ss << " 400 Bad Request\r\n";
    }

    ss << "Content-Type: application/json"
       << "\r\nContent-Length: " << body.size()
    //    << "\r\nAccess-Control-Allow-Origin: *"
    //    << "\r\nAccess-Control-Allow-Methods: POST, GET, OPTIONS"
    //    << "\r\nAccess-Control-Allow-Headers: Content-Type"
       << "\r\nConnection: " << "close\r\n"
       << "\r\n" << body;
    
    return ss.str();
}


OTAQueryDownloadRes::OTAQueryDownloadRes(float http_version)
    :OTAResponse(http_version){

}

std::string OTAQueryDownloadRes::build_http_response(bool success, nlohmann::json& json_response){
    bool connect = true;
    if(success){
        json_response["status"] = "ok";
    } else {
        json_response["status"] = "error";
        connect = false;
    }

    std::string body = json_response.dump();

    std::stringstream ss;
    ss << "HTTP/" << m_http_version;
    if(success){
        ss << " 200 OK\r\n";
    } else {
        ss << " 400 Bad Request\r\n";
    }

    if(json_response["download_status"] == "down"){
        connect = false;
    }

    ss << "Content-Type: application/json"
       << "\r\nContent-Length: " << body.size()
       << "\r\nConnection: " << (connect ? "keep-alive\r\n" : "close\r\n")
       << "\r\n" << body;
    
    return ss.str();
}

OTAFileDownloadRes::OTAFileDownloadRes(float http_version)
    :OTAResponse(http_version){

}


std::string OTAFileDownloadRes::build_http_response(bool success, nlohmann::json& json_response){
    if(!success){
        json_response["status"] = "error";
    }

    std::string body = json_response.dump();

    std::stringstream ss;
    ss << "HTTP/" << m_http_version;
    if(success){
        ss << " 200 OK\r\n";
    } else {
        ss << " 400 Bad Request\r\n";
    }

    ss << "Content-Type: application/octet-stream"
       << "\r\nContent-Length: " << json_response["file_size"]
       << "\r\nContent-Disposition: attachment; filename=" << "your_file_name.jpg"
       << "\r\nConnection: " << "close\r\n"
       << "\r\n";
    
    return ss.str();
}

HttpOptionsRes::HttpOptionsRes(float http_version)
    :OTAResponse(http_version){

}

std::string HttpOptionsRes::build_http_response(bool success, nlohmann::json& json_response) {
    if (!success) {
        json_response["status"] = "error";
    } else {
        json_response["status"] = "ok";
    }

    std::string body = "OK";

    std::stringstream ss;
    ss << "HTTP/" << m_http_version;
    if (success) {
        ss << " 200 OK\r\n";
    } else {
        ss << " 400 Bad Request\r\n";
    }

    // 关键 CORS 头，必须包含
    ss << "Access-Control-Allow-Origin: *\r\n";
    ss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    ss << "Access-Control-Allow-Headers: Content-Type\r\n";

    // 内容头（可选，OPTIONS 一般返回空或OK）
    ss << "Content-Type: text/plain\r\n";
    ss << "Content-Length: " << body.size() << "\r\n";
    ss << "Connection: close\r\n\r\n";

    ss << body;
    return ss.str();
}


OTAHttpResBuilder::OTAHttpResBuilder(float http_version){
    m_response.clear();
    m_response["notify"] = std::make_shared<OTANotifyRes>(http_version);
    m_response["query"] = std::make_shared<OTAQueryRes>(http_version);
    m_response["stop_notify"] = std::make_shared<OTAStopNotifyRes>(http_version);
    m_response["query_download"] = std::make_shared<OTAQueryDownloadRes>(http_version);
    m_response["file_download"] = std::make_shared<OTAFileDownloadRes>(http_version);
    m_response["OPTIONS"] = std::make_shared<HttpOptionsRes>(http_version);
}

std::string OTAHttpResBuilder::build_http_response(const std::string& type, bool success, nlohmann::json& json_response){
    auto it = m_response.find(type);
    if(it == m_response.end()){
        return "";
    }

    if(!(*it).second){
        return "";
    }
    
    return (*it).second->build_http_response(success, json_response);
}

}