#include "sherry/device/device_engine.h"
#include "sherry/device/device_camera.h"
#include "sherry/endian.h"
#include "sherry/log.h"

#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

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

static int writeFixSize(int fd, const void* buffer, size_t length) {
    size_t off = 0;
    const char* p = (const char*)buffer;
    while (off < length) {
        ssize_t rt = ::send(fd, p + off, length - off, 0);
        if (rt <= 0) return (int)rt;
        off += (size_t)rt;
    }
    return (int)off;
}

static int readFixSize(int fd, void* buffer, size_t length) {
    size_t off = 0;
    char* p = (char*)buffer;
    while (off < length) {
        ssize_t rt = ::recv(fd, p + off, length - off, 0);
        if (rt <= 0) return (int)rt;
        off += (size_t)rt;
    }
    return (int)off;
}

static uint16_t launch_server(std::thread& t, std::atomic<bool>& stop_flag) {
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        throw std::runtime_error("socket()");
    }

    int opt = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    if (::bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        ::close(listen_fd);
        throw std::runtime_error("bind()");
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(listen_fd, (sockaddr*)&addr, &len) != 0) {
        ::close(listen_fd);
        throw std::runtime_error("getsockname()");
    }

    uint16_t port = ntohs(addr.sin_port);

    if (::listen(listen_fd, 1) != 0) {
        ::close(listen_fd);
        throw std::runtime_error("listen()");
    }

    t = std::thread([listen_fd, &stop_flag]() {
        int client_fd = ::accept(listen_fd, nullptr, nullptr);
        ::close(listen_fd);
        if (client_fd < 0) {
            return;
        }

        while (!stop_flag.load()) {
            FrameHeader net_h;
            int rt = readFixSize(client_fd, &net_h, sizeof(net_h));
            if (rt <= 0) {
                break;
            }

            FrameHeader h = toHost(net_h);
            SYLAR_LOG_INFO(g_logger) << "server recv: car_id=" << h.car_id 
                                     << " seq=" << h.seq 
                                     << " image_nums=" << h.image_nums
                                     << " message_size=" << h.message_size;

            std::string payload;
            payload.resize(h.message_size);
            if (h.message_size > 0) {
                rt = readFixSize(client_fd, payload.data(), payload.size());
                if (rt <= 0) {
                    break;
                }
            }

            // 发送ACK响应
            std::string ack = std::string("ACK image bytes=") + std::to_string(payload.size());
            FrameHeader resp;
            resp.message_size = (uint32_t)ack.size();
            resp.image_nums = 0;
            resp.car_id = h.car_id;
            resp.seq = h.seq;
            FrameHeader net_resp = toNetwork(resp);
            if (writeFixSize(client_fd, &net_resp, sizeof(net_resp)) <= 0) break;
            if (writeFixSize(client_fd, ack.data(), ack.size()) <= 0) break;
        }

        ::close(client_fd);
    });

    return port;
}

} // namespace

int main() {
    std::atomic<bool> stop_server{false};
    std::thread server_thread;
    uint16_t port = launch_server(server_thread, stop_server);

    sherry::device::DeviceEngine::Options opt;
    opt.server_ip = "127.0.0.1";
    opt.server_port = port;
    opt.send_interval_ms = 1000;
    opt.car_id = 12345;  // 设置测试用车辆ID

    auto camera = std::make_shared<sherry::device::NaviCamera>(
        "/root/xxl/workspace/ota-new/file/navi_data/common");

    auto engine = std::make_shared<sherry::device::DeviceEngine>(opt, camera);
    engine->setServerMessageCallback([](const std::string& msg) {
        SYLAR_LOG_INFO(g_logger) << "server msg: " << msg;
    });

    if (!engine->start()) {
        SYLAR_LOG_ERROR(g_logger) << "failed to start engine";
        stop_server.store(true);
        server_thread.join();
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(3));

    engine->stop();

    stop_server.store(true);
    if (server_thread.joinable()) {
        server_thread.join();
    }

    SYLAR_LOG_INFO(g_logger) << "test_device_engine done";
    return 0;
}
