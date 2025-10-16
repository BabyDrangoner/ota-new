#ifndef _SHERRY_OTAHTTP_RESPONSE_BUILDER_H__
#define _SHERRY_OTAHTTP_RESPONSE_BUILDER_H__

#include "sherry.h"
#include "../include/json/json.hpp"

namespace sherry{

class OTAResponse{
public:
    typedef std::shared_ptr<OTAResponse> ptr;
    OTAResponse(float http_version);
    virtual ~OTAResponse(){}
    virtual std::string build_http_response(bool success, nlohmann::json& json_response) = 0;
protected:
    float m_http_version;
};

class OTANotifyRes : public OTAResponse{
public:
    typedef std::shared_ptr<OTANotifyRes> ptr;
    OTANotifyRes(float http_version);
    ~OTANotifyRes(){}

    virtual std::string build_http_response(bool success, nlohmann::json& json_response);
};

class OTAStopNotifyRes : public OTAResponse{
public:
    typedef std::shared_ptr<OTANotifyRes> ptr;
    OTAStopNotifyRes(float http_version);
    ~OTAStopNotifyRes(){}

    virtual std::string build_http_response(bool success, nlohmann::json& json_response);
};

class OTAQueryRes : public OTAResponse{
public:
    typedef std::shared_ptr<OTAQueryRes> ptr;
    OTAQueryRes(float http_version);
    ~OTAQueryRes(){}

    virtual std::string build_http_response(bool success, nlohmann::json& json_response);
};

class OTAQueryDownloadRes : public OTAResponse{
public:
    typedef std::shared_ptr<OTAQueryDownloadRes> ptr;
    OTAQueryDownloadRes(float http_version);
    ~OTAQueryDownloadRes(){}

    virtual std::string build_http_response(bool success, nlohmann::json& json_response);

};

class OTAFileDownloadRes : public OTAResponse{
public:
    typedef std::shared_ptr<OTAFileDownloadRes> ptr;
    OTAFileDownloadRes(float http_version);
    ~OTAFileDownloadRes(){}

    virtual std::string build_http_response(bool success, nlohmann::json& json_response);
};

class HttpOptionsRes : public OTAResponse{
public:
    typedef std::shared_ptr<OTAFileDownloadRes> ptr;
    HttpOptionsRes(float http_version);
    ~HttpOptionsRes(){}

    virtual std::string build_http_response(bool success, nlohmann::json& json_response);
};

class OTAHttpResBuilder{
public:
    typedef std::shared_ptr<OTAHttpResBuilder> ptr;
    OTAHttpResBuilder(float http_version);
    ~OTAHttpResBuilder(){};
    std::string build_http_response(const std::string& type, bool success, nlohmann::json& json_response);

private:
    std::unordered_map<std::string, OTAResponse::ptr> m_response;
};


}


#endif