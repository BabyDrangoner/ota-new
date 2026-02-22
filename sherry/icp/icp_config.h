#ifndef __SHERRY_ICP_CONFIG_H__
#define __SHERRY_ICP_CONFIG_H__

#include <cstdint>
#include <string>
#include <memory>

namespace sherry {
namespace icp {

/**
 * @brief vLLM 配置
 */
struct VllmConfig {
    std::string endpoint = "http://localhost:8000";   // vLLM 服务地址
    std::string model = "Qwen/Qwen3-VL-4B";           // 模型名称
    uint32_t max_tokens = 512;                         // 最大生成token数
    float temperature = 0.7f;                          // 温度参数
    float top_p = 0.8f;                                // Top-P 采样参数
    float repetition_penalty = 1.05f;                  // 重复惩罚参数
    uint32_t timeout_ms = 30000;                       // 请求超时(毫秒)
    bool enable_stream = false;                        // 是否启用流式输出
    uint32_t limit_mm_per_prompt = 4;                  // 每个prompt最大图片数
    
    // Qwen3-VL 图片分辨率控制
    uint32_t min_pixels = 256 * 28 * 28;               // 最小像素数
    uint32_t max_pixels = 1280 * 28 * 28;              // 最大像素数
    
    // 系统提示词
    std::string system_prompt = "You are a helpful assistant.";  // 系统提示
    
    // HTTP 连接池配置
    uint32_t max_connections = 10;                     // 最大连接数
    uint32_t connection_timeout_ms = 5000;             // 连接超时
};

/**
 * @brief ICP 服务配置
 */
struct IcpServerConfig {
    std::string bind_address = "0.0.0.0";              // 绑定地址
    uint16_t bind_port = 9000;                         // 绑定端口
    uint32_t recv_timeout_ms = 60000;                  // 接收超时(毫秒)
    uint32_t max_connections = 100;                    // 最大连接数
};

/**
 * @brief ICP 整体配置
 */
struct IcpConfig {
    typedef std::shared_ptr<IcpConfig> ptr;
    
    // 消息相关
    uint32_t max_msg_size = 10 * 1024 * 1024;          // 单条消息最大长度(10MB)
    uint16_t max_images_per_msg = 4;                   // 每条消息最大图片数
    
    // 控制相关
    uint32_t max_cars = 100;                           // 最大车辆数
    uint32_t min_submit_interval_ms = 0;               // 同车最小提交间隔(毫秒), 0表示不限制
    
    // 线程配置
    uint32_t io_threads = 2;                           // IO线程数
    uint32_t control_threads = 1;                      // 控制线程数
    uint32_t http_threads = 4;                         // HTTP请求线程数
    
    // 子配置
    VllmConfig vllm;                                   // vLLM配置
    IcpServerConfig server;                            // 服务器配置
    
    // 可观测性
    bool enable_metrics = true;                        // 是否启用指标收集
    uint32_t metrics_interval_ms = 1000;               // 指标输出间隔
    
    // Base64编码
    bool encode_images_base64 = true;                  // 是否将图片编码为base64
    
    /**
     * @brief 验证配置是否合法
     * @return 空字符串表示合法, 否则返回错误信息
     */
    std::string validate() const;
    
    /**
     * @brief 从YAML文件加载配置
     * @param filepath 配置文件路径
     * @return 配置指针, 加载失败返回nullptr
     */
    static IcpConfig::ptr loadFromFile(const std::string& filepath);
    
    /**
     * @brief 保存配置到YAML文件
     * @param filepath 配置文件路径
     * @return true表示成功
     */
    bool saveToFile(const std::string& filepath) const;
    
    /**
     * @brief 获取默认配置
     */
    static IcpConfig::ptr getDefault();
};

} // namespace icp
} // namespace sherry

#endif // __SHERRY_ICP_CONFIG_H__
