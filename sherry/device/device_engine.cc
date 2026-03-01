#include "sherry/device/device_engine.h"

#include "sherry/address.h"
#include "sherry/endian.h"
#include "sherry/log.h"
#include "sherry/icp/icp_protocol.h"

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace sherry {
namespace device {

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
static const char* TAG = "DeviceEngine";

// 使用 ICP 协议的类型（使用完全限定名称避免与 device::ImageType 冲突）
using icp::MessageHeader;
using icp::ImageMeta;
using icp::OutputMessage;
using icp::MessageBuilder;

namespace {

} // namespace

DeviceEngine::DeviceEngine(Options opt, Camera::ptr camera, IOManager::ptr io_mgr)
    : m_opt(std::move(opt))
    , m_camera(std::move(camera))
    , m_io_mgr(std::move(io_mgr))
    , m_car_id(m_opt.car_id)
    , m_seq(0) {
}

DeviceEngine::~DeviceEngine() {
    stop();
}

void DeviceEngine::setServerMessageCallback(std::function<void(const std::string&)> cb) {
    m_on_server_msg = std::move(cb);
}

void DeviceEngine::setIcpResultCallback(std::function<void(const icp::OutputMessage&)> cb) {
    m_on_icp_result = std::move(cb);
}

std::vector<uint8_t> DeviceEngine::buildIcpMessage() {
    // 获取相机需要的缓冲区大小
    size_t cam_buf_size = m_camera->get_buf_len();
    if (cam_buf_size == 0) {
        return {};
    }
    
    // 分配临时缓冲区获取相机数据
    std::vector<char> cam_buf(cam_buf_size);
    int mk = m_camera->make_images(cam_buf.data(), cam_buf.size());
    if (mk != 0) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] make_images failed ret=" << mk;
        return {};
    }
    
    // 使用 MessageBuilder 构建 ICP 消息
    MessageBuilder builder;
    builder.setCarId(static_cast<uint32_t>(m_car_id));
    builder.setSeq(m_seq.fetch_add(1));
    
    // 获取当前时间戳
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    builder.setTimestamp(static_cast<uint64_t>(ms));
    
    // 每条 ICP 消息打包 images_per_msg 张图片
    // 每次调用 make_images() 从 Camera 取下一帧（NaviCamera 会自动循环递增）
    int images_per_msg = m_opt.images_per_msg;
    if (images_per_msg <= 0) images_per_msg = 1;

    for (int i = 0; i < images_per_msg; ++i) {
        // 每帧独立申请缓冲（大小固定为 cam_buf_size），复用已有 cam_buf
        if (i > 0) {
            // 后续帧：重置缓冲区并再次调用 make_images
            std::fill(cam_buf.begin(), cam_buf.end(), 0);
            int mk2 = m_camera->make_images(cam_buf.data(), cam_buf.size());
            if (mk2 != 0) {
                SYLAR_LOG_WARN(g_logger) << "[" << TAG << "] make_images failed at frame " << i;
                break;
            }
        }

        // 解析单帧 Camera 输出: image_header + data
        const char* ptr = cam_buf.data();
        const char* end = cam_buf.data() + cam_buf.size();

        SingleCamera::image_header cam_header;
        if (ptr + sizeof(cam_header) > end) break;
        std::memcpy(&cam_header, ptr, sizeof(cam_header));
        ptr += sizeof(cam_header);

        if (cam_header.image_size == 0 || ptr + cam_header.image_size > end) break;

        // 转换图像类型：device::ImageType -> icp::ImageType
        icp::ImageType icp_type = icp::ImageType::UNKNOWN;
        switch (cam_header.type) {
            case device::ImageType::PNG:
            case device::ImageType::JPG:
                icp_type = icp::ImageType::RGB;
                break;
            case device::ImageType::DEEP:
                icp_type = icp::ImageType::DEPTH;
                break;
            default:
                icp_type = icp::ImageType::UNKNOWN;
                break;
        }

        builder.addImage(icp_type,
                         reinterpret_cast<const uint8_t*>(ptr),
                         cam_header.image_size);

        SYLAR_LOG_DEBUG(g_logger) << "[" << TAG << "] packed image[" << i
                                   << "] size=" << cam_header.image_size
                                   << " type=" << static_cast<int>(cam_header.type);
    }
    
    return builder.build();
}

bool DeviceEngine::connectSocket() {
    auto addr = sherry::IPv4Address::Create(m_opt.server_ip.c_str(), m_opt.server_port);
    if (!addr) {
        SYLAR_LOG_ERROR(g_logger) << "DeviceEngine: failed to create address "
                                  << m_opt.server_ip << ":" << m_opt.server_port;
        return false;
    }

    m_sock = sherry::Socket::CreateTCP(addr);
    if (!m_sock) {
        SYLAR_LOG_ERROR(g_logger) << "DeviceEngine: failed to create tcp socket";
        return false;
    }

    if (!m_sock->connect(addr, m_opt.connect_timeout_ms)) {
        SYLAR_LOG_ERROR(g_logger) << "DeviceEngine: connect failed "
                                  << m_opt.server_ip << ":" << m_opt.server_port;
        m_sock = nullptr;
        return false;
    }

    m_stream = std::make_shared<sherry::SocketStream>(m_sock, false);
    SYLAR_LOG_INFO(g_logger) << "DeviceEngine: connected to "
                             << m_opt.server_ip << ":" << m_opt.server_port;
    return true;
}

void DeviceEngine::closeSocket() {
    if (m_stream) {
        m_stream->close();
        m_stream = nullptr;
    }
    if (m_sock) {
        // shutdown() 先于 close()，可以可靠地唤醒阻塞在 recv/send 上的线程
        int fd = m_sock->getSocket();
        if (fd >= 0) {
            ::shutdown(fd, SHUT_RDWR);
        }
        m_sock->close();
        m_sock = nullptr;
    }
}

int DeviceEngine::writeFixSize(const void* buffer, size_t length) {
    if (!m_sock || !m_sock->isConnected()) {
        return -1;
    }

    size_t offset = 0;
    const char* p = static_cast<const char*>(buffer);
    while (offset < length && m_running.load()) {
        int rt = m_sock->send(p + offset, length - offset, 0);
        if (rt <= 0) {
            return rt;
        }
        offset += static_cast<size_t>(rt);
    }
    return static_cast<int>(offset);
}

int DeviceEngine::readFixSize(void* buffer, size_t length) {
    if (!m_sock || !m_sock->isConnected()) {
        return -1;
    }

    size_t offset = 0;
    char* p = static_cast<char*>(buffer);
    while (offset < length && m_running.load()) {
        int rt = m_sock->recv(p + offset, length - offset, 0);
        if (rt <= 0) {
            return rt;
        }
        offset += static_cast<size_t>(rt);
    }
    return static_cast<int>(offset);
}

bool DeviceEngine::start() {
    if (m_running.exchange(true)) {
        return true;
    }

    if (!m_camera) {
        SYLAR_LOG_ERROR(g_logger) << "DeviceEngine: camera is null";
        m_running.store(false);
        return false;
    }

    if (m_opt.server_port <= 0) {
        SYLAR_LOG_ERROR(g_logger) << "DeviceEngine: invalid server_port=" << m_opt.server_port;
        m_running.store(false);
        return false;
    }

    if (!connectSocket()) {
        m_running.store(false);
        return false;
    }

    m_send_thread = std::make_shared<sherry::Thread>(
        std::bind(&DeviceEngine::sendLoop, this),
        "device_send");

    m_recv_thread = std::make_shared<sherry::Thread>(
        std::bind(&DeviceEngine::recvLoop, this),
        "device_recv");

    return true;
}

void DeviceEngine::stop() {
    bool was_running = m_running.exchange(false);
    if (!was_running) {
        return;
    }

    closeSocket();

    if (m_send_thread) {
        m_send_thread->join();
        m_send_thread = nullptr;
    }

    if (m_recv_thread) {
        m_recv_thread->join();
        m_recv_thread = nullptr;
    }
}

void DeviceEngine::sendLoop() {
    while (m_running.load()) {
        if (!m_sock || !m_sock->isConnected()) {
            SYLAR_LOG_WARN(g_logger) << "[" << TAG << "] sendLoop: socket disconnected";
            break;
        }

        // 构建 ICP 协议格式的消息
        std::vector<uint8_t> message = buildIcpMessage();
        if (message.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_opt.send_interval_ms));
            continue;
        }

        // 发送完整消息（ICP 协议包含 header）
        int ret = writeFixSize(message.data(), message.size());
        if (ret <= 0) {
            SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] sendLoop: write failed ret=" << ret;
            break;
        }

        SYLAR_LOG_DEBUG(g_logger) << "[" << TAG << "] sendLoop: sent ICP message, size=" << message.size();
        std::this_thread::sleep_for(std::chrono::milliseconds(m_opt.send_interval_ms));
    }

    // Only signal to stop, let stop() handle cleanup
    m_running.store(false);
}

void DeviceEngine::recvLoop() {
    // ICP 响应使用简单的 JSON 格式，前面有 4 字节长度
    while (m_running.load()) {
        if (!m_sock || !m_sock->isConnected()) {
            break;
        }

        // 读取消息长度（4 字节）
        uint32_t msg_len = 0;
        int ret = readFixSize(&msg_len, sizeof(msg_len));
        if (ret <= 0) {
            SYLAR_LOG_WARN(g_logger) << "[" << TAG << "] recvLoop: read length failed ret=" << ret;
            break;
        }

        if (msg_len > m_opt.max_payload_bytes) {
            SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] recvLoop: message too large len=" << msg_len;
            break;
        }

        // 读取 JSON 消息体
        std::vector<char> payload(msg_len);
        if (msg_len > 0) {
            ret = readFixSize(payload.data(), payload.size());
            if (ret <= 0) {
                SYLAR_LOG_WARN(g_logger) << "[" << TAG << "] recvLoop: read payload failed ret=" << ret;
                break;
            }
        }

        // 解析 OutputMessage
        std::string json_str(payload.data(), payload.size());
        
        // 尝试调用 ICP 结果回调
        if (m_on_icp_result) {
            OutputMessage output = OutputMessage::fromJson(json_str);
            m_on_icp_result(output);
        }
        
        // 也调用旧的回调（兼容性）
        if (m_on_server_msg) {
            m_on_server_msg(json_str);
        }
        
        if (!m_on_icp_result && !m_on_server_msg) {
            SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] recvLoop: received response: " << json_str;
        }
    }

    // Only signal to stop, let stop() handle cleanup
    m_running.store(false);
}

} // namespace device
} // namespace sherry
