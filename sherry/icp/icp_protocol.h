#ifndef __SHERRY_ICP_PROTOCOL_H__
#define __SHERRY_ICP_PROTOCOL_H__

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <cstring>

namespace sherry {
namespace icp {

/**
 * @brief 图片类型枚举
 */
enum class ImageType : uint8_t {
    RGB = 0,
    DEPTH = 1,
    UNKNOWN = 255
};

/**
 * @brief 消息头结构
 * 
 * 协议格式:
 * +---------------+---------------+---------------+---------------+
 * |   magic (4)   | message_size  |   car_id (4)  |    seq (8)    |
 * +---------------+---------------+---------------+---------------+
 * | timestamp_ms  | image_nums(2) | prompt_len(2) |   reserved    |
 * +---------------+---------------+---------------+---------------+
 * 
 * 总大小: 32 bytes
 */
#pragma pack(push, 1)
struct MessageHeader {
    uint32_t magic;           // 魔数 'ICPM' = 0x4D504349
    uint32_t message_size;    // 整条消息大小(包含header)
    uint32_t car_id;          // 车辆ID
    uint64_t seq;             // 序列号(单调递增)
    uint64_t timestamp_ms;    // 时间戳(毫秒)
    uint16_t image_nums;      // 图片数量
    uint16_t prompt_len;      // prompt长度(可选,0表示无)
    uint32_t reserved;        // 保留字段
    
    static constexpr uint32_t MAGIC = 0x4D504349;  // 'ICPM'
    static constexpr size_t SIZE = 32;
    
    bool isValid() const { return magic == MAGIC; }
};

/**
 * @brief 图片元数据结构
 * 
 * +---------------+---------------+
 * | image_type(1) | reserved (3)  |
 * +---------------+---------------+
 * |      image_size (4 bytes)     |
 * +---------------+---------------+
 * 
 * 总大小: 8 bytes
 */
struct ImageMeta {
    uint8_t image_type;       // 图片类型 (RGB/DEPTH)
    uint8_t reserved[3];      // 保留
    uint32_t image_size;      // 图片数据大小
    
    static constexpr size_t SIZE = 8;
    
    ImageType getType() const { 
        return static_cast<ImageType>(image_type); 
    }
};
#pragma pack(pop)

/**
 * @brief 图片视图结构(零拷贝)
 */
struct ImageView {
    ImageType type;
    const uint8_t* data;
    size_t size;
};

/**
 * @brief 解析后的消息结构
 */
struct ParsedMessage {
    typedef std::shared_ptr<ParsedMessage> ptr;
    
    uint32_t car_id;
    uint64_t seq;
    uint64_t timestamp_ms;
    std::vector<ImageView> images;
    std::string prompt;
    
    // 原始数据的引用(用于避免拷贝)
    const uint8_t* raw_data;
    size_t raw_size;
};

/**
 * @brief 消息解析器
 */
class MessageParser {
public:
    typedef std::shared_ptr<MessageParser> ptr;
    
    enum class ParseResult {
        OK = 0,
        NEED_MORE_DATA = 1,
        INVALID_MAGIC = 2,
        INVALID_SIZE = 3,
        INVALID_IMAGE_COUNT = 4,
        BUFFER_OVERFLOW = 5
    };
    
    /**
     * @brief 仅解析消息头(用于Early Drop)
     * @param data 数据指针
     * @param len 数据长度
     * @param header 输出的消息头
     * @return 解析结果
     */
    static ParseResult parseHeader(const uint8_t* data, size_t len, 
                                   MessageHeader& header) {
        if (len < MessageHeader::SIZE) {
            return ParseResult::NEED_MORE_DATA;
        }
        
        std::memcpy(&header, data, MessageHeader::SIZE);
        
        if (!header.isValid()) {
            return ParseResult::INVALID_MAGIC;
        }
        
        return ParseResult::OK;
    }
    
    /**
     * @brief 获取消息所需的总大小
     */
    static size_t getMessageSize(const uint8_t* data, size_t len) {
        if (len < MessageHeader::SIZE) {
            return 0;
        }
        MessageHeader header;
        std::memcpy(&header, data, MessageHeader::SIZE);
        return header.message_size;
    }
    
    /**
     * @brief 完整解析消息(零拷贝)
     * @param data 数据指针
     * @param len 数据长度
     * @param msg 输出的解析消息
     * @return 解析结果
     */
    static ParseResult parseMessage(const uint8_t* data, size_t len,
                                    ParsedMessage& msg) {
        MessageHeader header;
        auto result = parseHeader(data, len, header);
        if (result != ParseResult::OK) {
            return result;
        }
        
        if (len < header.message_size) {
            return ParseResult::NEED_MORE_DATA;
        }
        
        msg.car_id = header.car_id;
        msg.seq = header.seq;
        msg.timestamp_ms = header.timestamp_ms;
        msg.raw_data = data;
        msg.raw_size = header.message_size;
        
        size_t offset = MessageHeader::SIZE;
        
        // 解析每张图片
        msg.images.clear();
        msg.images.reserve(header.image_nums);
        
        for (uint16_t i = 0; i < header.image_nums; ++i) {
            if (offset + ImageMeta::SIZE > len) {
                return ParseResult::BUFFER_OVERFLOW;
            }
            
            ImageMeta meta;
            std::memcpy(&meta, data + offset, ImageMeta::SIZE);
            offset += ImageMeta::SIZE;
            
            if (offset + meta.image_size > len) {
                return ParseResult::BUFFER_OVERFLOW;
            }
            
            ImageView view;
            view.type = meta.getType();
            view.data = data + offset;
            view.size = meta.image_size;
            msg.images.push_back(view);
            
            offset += meta.image_size;
        }
        
        // 解析prompt
        if (header.prompt_len > 0) {
            if (offset + header.prompt_len > len) {
                return ParseResult::BUFFER_OVERFLOW;
            }
            msg.prompt.assign(reinterpret_cast<const char*>(data + offset), 
                             header.prompt_len);
        } else {
            msg.prompt.clear();
        }
        
        return ParseResult::OK;
    }
};

/**
 * @brief 输出消息结构(ICP -> 设备)
 */
struct OutputMessage {
    uint32_t car_id;
    uint64_t seq;
    std::string type;                   // "complete" | "stream_token" | "stream_batch"
    std::string status;                 // complete 类型时: "success", "error"
    uint64_t latency_ms;
    std::string waypoints;              // complete 时完整文本
    std::string token;                  // stream_token 单个token
    std::vector<std::string> tokens;    // stream_batch 批次tokens
    
    std::string toJson() const;
    static OutputMessage fromJson(const std::string& json);
};

/**
 * @brief 消息构建器(用于单元测试和设备模拟)
 */
class MessageBuilder {
public:
    MessageBuilder();
    
    MessageBuilder& setCarId(uint32_t car_id);
    MessageBuilder& setSeq(uint64_t seq);
    MessageBuilder& setTimestamp(uint64_t ts_ms);
    MessageBuilder& addImage(ImageType type, const uint8_t* data, size_t size);
    MessageBuilder& setPrompt(const std::string& prompt);
    
    std::vector<uint8_t> build();
    
private:
    uint32_t m_carId = 0;
    uint64_t m_seq = 0;
    uint64_t m_timestampMs = 0;
    std::string m_prompt;
    std::vector<std::pair<ImageType, std::vector<uint8_t>>> m_images;
};

} // namespace icp
} // namespace sherry

#endif // __SHERRY_ICP_PROTOCOL_H__
