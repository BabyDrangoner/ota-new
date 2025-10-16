#ifndef _SHERRY_HTTP_SENDBUFFER_H__
#define _SHERRY_HTTP_SENDBUFFER_H__

#include "../sherry.h"
#include "../socket.h"
#include "../../include/json/json.hpp"

#include <unordered_map>
#include <vector>
#include <memory>

namespace sherry {

struct DeviceInstance {
    nlohmann::json download_details;
    nlohmann::json query_details;
};

struct OTAData {
    std::string manager_info;

    std::unordered_map<int, std::unordered_map<int, std::unordered_map<std::string, DeviceInstance>>> devices;

    void mergeFromJson(const std::string& json_str) {
        try {
            auto j = nlohmann::json::parse(json_str);
            int device_type = j.at("device_type");
            int device_no = j.at("device_no");
            std::string name = j.at("name");

            DeviceInstance& instance = devices[device_type][device_no][name];

            if (j.contains("download_details")) {
                instance.download_details = j["download_details"];
            }
            if (j.contains("query_details")) {
                instance.query_details = j["query_details"];
            }


        } catch (const std::exception& e) {
            std::cerr << "[OTAData::mergeFromJson] parse/merge failed: " << e.what() << std::endl;
        }
    }

    nlohmann::json to_json() const {
        nlohmann::json j;
        j["manager_info"] = manager_info;

        for (const auto& [type, type_map] : devices) {
            for (const auto& [no, no_map] : type_map) {
                for (const auto& [name, inst] : no_map) {
                    j["devices"][std::to_string(type)][std::to_string(no)][name] = {
                        {"download_details", inst.download_details},
                        {"query_details", inst.query_details}
                    };
                }
            }
        }
        return j;
    }

    std::string to_string() const {
        return to_json().dump(); // 可选 .dump(4) 变成带缩进的
    }
};


class HttpSocketBuffer {
public:
    typedef std::shared_ptr<HttpSocketBuffer> ptr;
    typedef Mutex MutexType;

    HttpSocketBuffer() = default;
    ~HttpSocketBuffer() = default;

    void addData(const std::string& data);
    std::string getData();

private:
    MutexType m_mutex;
    OTAData m_data;
};

class HttpServer;

class HttpSendBuffer {
public:
    typedef std::shared_ptr<HttpSendBuffer> ptr;
    typedef Mutex MutexType;

    HttpSendBuffer(){}
    HttpSendBuffer(HttpServer* server);


    ~HttpSendBuffer() {
        m_mp.clear();
    }

    void addData(int key, const std::string data);

    std::string getData(int key);

private:
    MutexType m_mutex;
    std::unordered_map<int, HttpSocketBuffer::ptr> m_mp;
    HttpServer* m_server;
};

}

#endif
