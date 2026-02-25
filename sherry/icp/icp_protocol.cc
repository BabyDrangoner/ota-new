#include "icp_protocol.h"
#include "json/json.hpp"
#include <sstream>
#include <chrono>

namespace sherry {
namespace icp {

//------------------------------------------------------------------------------
// OutputMessage
//------------------------------------------------------------------------------

std::string OutputMessage::toJson() const {
    nlohmann::json j;
    j["car_id"]    = car_id;
    j["seq"]       = seq;
    j["type"]      = type.empty() ? "complete" : type;
    j["status"]    = status;
    j["latency_ms"]= latency_ms;
    if (!token.empty()) {
        j["token"] = token;
    }
    if (!tokens.empty()) {
        j["tokens"] = tokens;   // nlohmann 直接序列化 vector<string>
    }
    if (!waypoints.empty()) {
        // 尝试解析 waypoints 为 JSON 对象
        try {
            j["waypoints"] = nlohmann::json::parse(waypoints);
        } catch (...) {
            j["waypoints"] = waypoints;
        }
    }
    return j.dump();
}

OutputMessage OutputMessage::fromJson(const std::string& json) {
    OutputMessage msg;
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        
        if (j.contains("car_id")) {
            msg.car_id = j["car_id"].get<uint32_t>();
        }
        if (j.contains("seq")) {
            msg.seq = j["seq"].get<uint64_t>();
        }
        if (j.contains("type")) {
            msg.type = j["type"].get<std::string>();
        }
        if (j.contains("status")) {
            msg.status = j["status"].get<std::string>();
        }
        if (j.contains("latency_ms")) {
            msg.latency_ms = j["latency_ms"].get<uint64_t>();
        }
        if (j.contains("token")) {
            msg.token = j["token"].get<std::string>();
        }
        if (j.contains("tokens") && j["tokens"].is_array()) {
            msg.tokens = j["tokens"].get<std::vector<std::string>>();
        }
        if (j.contains("waypoints")) {
            if (j["waypoints"].is_string()) {
                msg.waypoints = j["waypoints"].get<std::string>();
            } else {
                msg.waypoints = j["waypoints"].dump();
            }
        }
    } catch (const std::exception& e) {
        // 解析失败，返回空消息
    }
    return msg;
}

//------------------------------------------------------------------------------
// MessageBuilder
//------------------------------------------------------------------------------

MessageBuilder::MessageBuilder() {
    auto now = std::chrono::system_clock::now();
    m_timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

MessageBuilder& MessageBuilder::setCarId(uint32_t car_id) {
    m_carId = car_id;
    return *this;
}

MessageBuilder& MessageBuilder::setSeq(uint64_t seq) {
    m_seq = seq;
    return *this;
}

MessageBuilder& MessageBuilder::setTimestamp(uint64_t ts_ms) {
    m_timestampMs = ts_ms;
    return *this;
}

MessageBuilder& MessageBuilder::addImage(ImageType type, const uint8_t* data, size_t size) {
    std::vector<uint8_t> img_data(data, data + size);
    m_images.emplace_back(type, std::move(img_data));
    return *this;
}

MessageBuilder& MessageBuilder::setPrompt(const std::string& prompt) {
    m_prompt = prompt;
    return *this;
}

std::vector<uint8_t> MessageBuilder::build() {
    // 计算总大小
    size_t total_size = MessageHeader::SIZE;
    for (const auto& img : m_images) {
        total_size += ImageMeta::SIZE + img.second.size();
    }
    total_size += m_prompt.size();
    
    std::vector<uint8_t> buffer(total_size);
    
    // 填充消息头
    MessageHeader header;
    header.magic = MessageHeader::MAGIC;
    header.message_size = static_cast<uint32_t>(total_size);
    header.car_id = m_carId;
    header.seq = m_seq;
    header.timestamp_ms = m_timestampMs;
    header.image_nums = static_cast<uint16_t>(m_images.size());
    header.prompt_len = static_cast<uint16_t>(m_prompt.size());
    header.reserved = 0;
    
    std::memcpy(buffer.data(), &header, MessageHeader::SIZE);
    size_t offset = MessageHeader::SIZE;
    
    // 填充图片数据
    for (const auto& img : m_images) {
        ImageMeta meta;
        meta.image_type = static_cast<uint8_t>(img.first);
        std::memset(meta.reserved, 0, sizeof(meta.reserved));
        meta.image_size = static_cast<uint32_t>(img.second.size());
        
        std::memcpy(buffer.data() + offset, &meta, ImageMeta::SIZE);
        offset += ImageMeta::SIZE;
        
        std::memcpy(buffer.data() + offset, img.second.data(), img.second.size());
        offset += img.second.size();
    }
    
    // 填充prompt
    if (!m_prompt.empty()) {
        std::memcpy(buffer.data() + offset, m_prompt.data(), m_prompt.size());
    }
    
    return buffer;
}

} // namespace icp
} // namespace sherry
