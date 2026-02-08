#include "sherry/device/device_engine.h"

#include "sherry/address.h"
#include "sherry/endian.h"
#include "sherry/log.h"

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace sherry {
namespace device {

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

namespace {

#pragma pack(push, 1)
struct FrameHeader {
    uint32_t message_size;  // 消息总大小（不含header）
    uint16_t image_nums;    // 图片数量
    uint64_t car_id;        // 车辆ID
    uint64_t seq;           // 单车递增序列号
};
#pragma pack(pop)

static FrameHeader toNetwork(FrameHeader h) {
    h.message_size = sherry::byteswapOnLittleEndian(h.message_size);
    h.image_nums = sherry::byteswapOnLittleEndian(h.image_nums);
    h.car_id = sherry::byteswapOnLittleEndian(h.car_id);
    h.seq = sherry::byteswapOnLittleEndian(h.seq);
    return h;
}

static FrameHeader toHost(FrameHeader h) {
    h.message_size = sherry::byteswapOnLittleEndian(h.message_size);
    h.image_nums = sherry::byteswapOnLittleEndian(h.image_nums);
    h.car_id = sherry::byteswapOnLittleEndian(h.car_id);
    h.seq = sherry::byteswapOnLittleEndian(h.seq);
    return h;
}

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
    std::vector<char> payload;

    while (m_running.load()) {
        if (!m_sock || !m_sock->isConnected()) {
            SYLAR_LOG_WARN(g_logger) << "DeviceEngine(send): socket disconnected";
            break;
        }

        size_t need = m_camera->get_buf_len();
        if (need == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_opt.send_interval_ms));
            continue;
        }

        payload.resize(need);
        int mk = m_camera->make_images(payload.data(), payload.size());
        if (mk != 0) {
            SYLAR_LOG_ERROR(g_logger) << "DeviceEngine(send): make_images failed ret=" << mk;
            std::this_thread::sleep_for(std::chrono::milliseconds(m_opt.send_interval_ms));
            continue;
        }

        FrameHeader h;
        h.message_size = static_cast<uint32_t>(payload.size());
        h.image_nums = static_cast<uint16_t>(m_camera->get_image_nums());
        h.car_id = m_car_id;
        h.seq = m_seq.fetch_add(1);
        FrameHeader net = toNetwork(h);

        int ret = writeFixSize(&net, sizeof(net));
        if (ret <= 0) {
            SYLAR_LOG_ERROR(g_logger) << "DeviceEngine(send): write header failed ret=" << ret;
            break;
        }

        ret = writeFixSize(payload.data(), payload.size());
        if (ret <= 0) {
            SYLAR_LOG_ERROR(g_logger) << "DeviceEngine(send): write payload failed ret=" << ret;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(m_opt.send_interval_ms));
    }

    // Only signal to stop, let stop() handle cleanup
    m_running.store(false);
}

void DeviceEngine::recvLoop() {
    while (m_running.load()) {
        if (!m_sock || !m_sock->isConnected()) {
            break;
        }

        FrameHeader net_h;
        int ret = readFixSize(&net_h, sizeof(net_h));
        if (ret <= 0) {
            SYLAR_LOG_WARN(g_logger) << "DeviceEngine(recv): read header failed ret=" << ret;
            break;
        }

        FrameHeader h = toHost(net_h);
        if (h.message_size > m_opt.max_payload_bytes) {
            SYLAR_LOG_ERROR(g_logger) << "DeviceEngine(recv): payload too large len=" << h.message_size;
            break;
        }

        std::vector<char> payload;
        payload.resize(h.message_size);
        if (h.message_size > 0) {
            ret = readFixSize(payload.data(), payload.size());
            if (ret <= 0) {
                SYLAR_LOG_WARN(g_logger) << "DeviceEngine(recv): read payload failed ret=" << ret;
                break;
            }
        }

        // 处理接收到的消息
        std::string msg(payload.data(), payload.data() + payload.size());
        if (m_on_server_msg) {
            m_on_server_msg(msg);
        } else {
            SYLAR_LOG_INFO(g_logger) << "DeviceEngine(recv): msg from car_id=" << h.car_id 
                                     << " seq=" << h.seq << " len=" << payload.size();
        }
    }

    // Only signal to stop, let stop() handle cleanup
    m_running.store(false);
}

} // namespace device
} // namespace sherry
