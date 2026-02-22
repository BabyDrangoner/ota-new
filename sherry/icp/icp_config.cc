#include "icp_config.h"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <sstream>

namespace sherry {
namespace icp {

std::string IcpConfig::validate() const {
    std::ostringstream errors;
    
    if (max_msg_size < 1024) {
        errors << "max_msg_size must be at least 1KB; ";
    }
    
    if (max_msg_size > 100 * 1024 * 1024) {
        errors << "max_msg_size must not exceed 100MB; ";
    }
    
    if (max_images_per_msg == 0 || max_images_per_msg > 16) {
        errors << "max_images_per_msg must be between 1 and 16; ";
    }
    
    if (max_cars == 0 || max_cars > 10000) {
        errors << "max_cars must be between 1 and 10000; ";
    }
    
    if (io_threads == 0 || io_threads > 32) {
        errors << "io_threads must be between 1 and 32; ";
    }
    
    if (control_threads == 0 || control_threads > 16) {
        errors << "control_threads must be between 1 and 16; ";
    }
    
    if (http_threads == 0 || http_threads > 64) {
        errors << "http_threads must be between 1 and 64; ";
    }
    
    if (vllm.endpoint.empty()) {
        errors << "vllm.endpoint must not be empty; ";
    }
    
    if (vllm.max_tokens == 0 || vllm.max_tokens > 8192) {
        errors << "vllm.max_tokens must be between 1 and 8192; ";
    }
    
    if (vllm.timeout_ms < 1000) {
        errors << "vllm.timeout_ms must be at least 1000ms; ";
    }
    
    if (server.bind_port == 0) {
        errors << "server.bind_port must not be 0; ";
    }
    
    return errors.str();
}

IcpConfig::ptr IcpConfig::loadFromFile(const std::string& filepath) {
    try {
        YAML::Node root = YAML::LoadFile(filepath);
        auto config = std::make_shared<IcpConfig>();
        
        // 解析消息配置
        if (root["max_msg_size"]) {
            config->max_msg_size = root["max_msg_size"].as<uint32_t>();
        }
        if (root["max_images_per_msg"]) {
            config->max_images_per_msg = root["max_images_per_msg"].as<uint16_t>();
        }
        
        // 解析控制配置
        if (root["max_cars"]) {
            config->max_cars = root["max_cars"].as<uint32_t>();
        }
        if (root["min_submit_interval_ms"]) {
            config->min_submit_interval_ms = root["min_submit_interval_ms"].as<uint32_t>();
        }
        
        // 解析线程配置
        if (root["io_threads"]) {
            config->io_threads = root["io_threads"].as<uint32_t>();
        }
        if (root["control_threads"]) {
            config->control_threads = root["control_threads"].as<uint32_t>();
        }
        if (root["http_threads"]) {
            config->http_threads = root["http_threads"].as<uint32_t>();
        }
        
        // 解析vLLM配置
        if (root["vllm"]) {
            auto vllm_node = root["vllm"];
            if (vllm_node["endpoint"]) {
                config->vllm.endpoint = vllm_node["endpoint"].as<std::string>();
            }
            if (vllm_node["model"]) {
                config->vllm.model = vllm_node["model"].as<std::string>();
            }
            if (vllm_node["max_tokens"]) {
                config->vllm.max_tokens = vllm_node["max_tokens"].as<uint32_t>();
            }
            if (vllm_node["temperature"]) {
                config->vllm.temperature = vllm_node["temperature"].as<float>();
            }
            if (vllm_node["timeout_ms"]) {
                config->vllm.timeout_ms = vllm_node["timeout_ms"].as<uint32_t>();
            }
            if (vllm_node["enable_stream"]) {
                config->vllm.enable_stream = vllm_node["enable_stream"].as<bool>();
            }
            if (vllm_node["limit_mm_per_prompt"]) {
                config->vllm.limit_mm_per_prompt = vllm_node["limit_mm_per_prompt"].as<uint32_t>();
            }
            if (vllm_node["max_connections"]) {
                config->vllm.max_connections = vllm_node["max_connections"].as<uint32_t>();
            }
            if (vllm_node["connection_timeout_ms"]) {
                config->vllm.connection_timeout_ms = vllm_node["connection_timeout_ms"].as<uint32_t>();
            }
            if (vllm_node["top_p"]) {
                config->vllm.top_p = vllm_node["top_p"].as<float>();
            }
            if (vllm_node["repetition_penalty"]) {
                config->vllm.repetition_penalty = vllm_node["repetition_penalty"].as<float>();
            }
            if (vllm_node["min_pixels"]) {
                config->vllm.min_pixels = vllm_node["min_pixels"].as<uint32_t>();
            }
            if (vllm_node["max_pixels"]) {
                config->vllm.max_pixels = vllm_node["max_pixels"].as<uint32_t>();
            }
            if (vllm_node["system_prompt"]) {
                config->vllm.system_prompt = vllm_node["system_prompt"].as<std::string>();
            }
        }
        
        // 解析服务器配置
        if (root["server"]) {
            auto server_node = root["server"];
            if (server_node["bind_address"]) {
                config->server.bind_address = server_node["bind_address"].as<std::string>();
            }
            if (server_node["bind_port"]) {
                config->server.bind_port = server_node["bind_port"].as<uint16_t>();
            }
            if (server_node["recv_timeout_ms"]) {
                config->server.recv_timeout_ms = server_node["recv_timeout_ms"].as<uint32_t>();
            }
            if (server_node["max_connections"]) {
                config->server.max_connections = server_node["max_connections"].as<uint32_t>();
            }
        }
        
        // 解析可观测性配置
        if (root["enable_metrics"]) {
            config->enable_metrics = root["enable_metrics"].as<bool>();
        }
        if (root["metrics_interval_ms"]) {
            config->metrics_interval_ms = root["metrics_interval_ms"].as<uint32_t>();
        }
        if (root["encode_images_base64"]) {
            config->encode_images_base64 = root["encode_images_base64"].as<bool>();
        }
        
        // 验证配置
        std::string error = config->validate();
        if (!error.empty()) {
            return nullptr;
        }
        
        return config;
    } catch (const std::exception& e) {
        return nullptr;
    }
}

bool IcpConfig::saveToFile(const std::string& filepath) const {
    try {
        YAML::Emitter out;
        out << YAML::BeginMap;
        
        out << YAML::Key << "max_msg_size" << YAML::Value << max_msg_size;
        out << YAML::Key << "max_images_per_msg" << YAML::Value << max_images_per_msg;
        out << YAML::Key << "max_cars" << YAML::Value << max_cars;
        out << YAML::Key << "min_submit_interval_ms" << YAML::Value << min_submit_interval_ms;
        out << YAML::Key << "io_threads" << YAML::Value << io_threads;
        out << YAML::Key << "control_threads" << YAML::Value << control_threads;
        out << YAML::Key << "http_threads" << YAML::Value << http_threads;
        out << YAML::Key << "enable_metrics" << YAML::Value << enable_metrics;
        out << YAML::Key << "metrics_interval_ms" << YAML::Value << metrics_interval_ms;
        out << YAML::Key << "encode_images_base64" << YAML::Value << encode_images_base64;
        
        // vLLM配置
        out << YAML::Key << "vllm" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "endpoint" << YAML::Value << vllm.endpoint;
        out << YAML::Key << "model" << YAML::Value << vllm.model;
        out << YAML::Key << "max_tokens" << YAML::Value << vllm.max_tokens;
        out << YAML::Key << "temperature" << YAML::Value << vllm.temperature;
        out << YAML::Key << "top_p" << YAML::Value << vllm.top_p;
        out << YAML::Key << "repetition_penalty" << YAML::Value << vllm.repetition_penalty;
        out << YAML::Key << "timeout_ms" << YAML::Value << vllm.timeout_ms;
        out << YAML::Key << "enable_stream" << YAML::Value << vllm.enable_stream;
        out << YAML::Key << "limit_mm_per_prompt" << YAML::Value << vllm.limit_mm_per_prompt;
        out << YAML::Key << "min_pixels" << YAML::Value << vllm.min_pixels;
        out << YAML::Key << "max_pixels" << YAML::Value << vllm.max_pixels;
        out << YAML::Key << "system_prompt" << YAML::Value << vllm.system_prompt;
        out << YAML::Key << "max_connections" << YAML::Value << vllm.max_connections;
        out << YAML::Key << "connection_timeout_ms" << YAML::Value << vllm.connection_timeout_ms;
        out << YAML::EndMap;
        
        // 服务器配置
        out << YAML::Key << "server" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "bind_address" << YAML::Value << server.bind_address;
        out << YAML::Key << "bind_port" << YAML::Value << server.bind_port;
        out << YAML::Key << "recv_timeout_ms" << YAML::Value << server.recv_timeout_ms;
        out << YAML::Key << "max_connections" << YAML::Value << server.max_connections;
        out << YAML::EndMap;
        
        out << YAML::EndMap;
        
        std::ofstream fout(filepath);
        if (!fout.is_open()) {
            return false;
        }
        fout << out.c_str();
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

IcpConfig::ptr IcpConfig::getDefault() {
    return std::make_shared<IcpConfig>();
}

} // namespace icp
} // namespace sherry
