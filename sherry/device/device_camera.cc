#include "device_camera.h"
#include "sherry/log.h"
#include <fstream>
#include <cstring>
#include <filesystem>
#include <algorithm>

namespace sherry {
namespace device {

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
static const char* TAG = "DeviceCamera";

// ====================================================================================
// SingleCamera Implementation
// ====================================================================================

SingleCamera::SingleCamera(int rgb_nums, int deep_nums,
                           const std::string& rgb_image_path,
                           const std::string& deep_image_path)
    : m_rgb_image_nums(rgb_nums)
    , m_deep_image_nums(deep_nums)
    , m_need_buf_len(0)
    , m_rgb_image(nullptr)
    , m_rgb_image_size(0)
    , m_deep_image(nullptr)
    , m_deep_image_size(0) {
    
    // 加载 RGB 图片到内存
    if (!rgb_image_path.empty() && m_rgb_image_nums > 0) {
        std::ifstream rgb_file(rgb_image_path, std::ios::binary | std::ios::ate);
        if (rgb_file.is_open()) {
            m_rgb_image_size = static_cast<size_t>(rgb_file.tellg());
            rgb_file.seekg(0, std::ios::beg);
            
            m_rgb_image = std::shared_ptr<char[]>(new char[m_rgb_image_size]);
            if (rgb_file.read(m_rgb_image.get(), m_rgb_image_size)) {
                SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] " << "loaded RGB image from " 
                                          << rgb_image_path 
                                          << ", size: " << m_rgb_image_size << " bytes";
            } else {
                SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] " << "failed to read RGB image from " 
                                           << rgb_image_path;
                m_rgb_image.reset();
                m_rgb_image_size = 0;
            }
            rgb_file.close();
        } else {
            SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] " << "failed to open RGB image file: " 
                                       << rgb_image_path;
        }
    }
    
    // 加载 Deep 图片到内存
    if (!deep_image_path.empty() && m_deep_image_nums > 0) {
        std::ifstream deep_file(deep_image_path, std::ios::binary | std::ios::ate);
        if (deep_file.is_open()) {
            m_deep_image_size = static_cast<size_t>(deep_file.tellg());
            deep_file.seekg(0, std::ios::beg);
            
            m_deep_image = std::shared_ptr<char[]>(new char[m_deep_image_size]);
            if (deep_file.read(m_deep_image.get(), m_deep_image_size)) {
                SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] " << "loaded Deep image from " 
                                          << deep_image_path 
                                          << ", size: " << m_deep_image_size << " bytes";
            } else {
                SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] " << "failed to read Deep image from " 
                                           << deep_image_path;
                m_deep_image.reset();
                m_deep_image_size = 0;
            }
            deep_file.close();
        } else {
            SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] " << "failed to open Deep image file: " 
                                       << deep_image_path;
        }
    }
    
    // 计算需要的缓冲区大小: (image_header + image_data) * 数量
    m_need_buf_len = (sizeof(image_header) + m_rgb_image_size) * m_rgb_image_nums
                   + (sizeof(image_header) + m_deep_image_size) * m_deep_image_nums;
    
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] " << "created: rgb_nums=" << m_rgb_image_nums
                              << ", deep_nums=" << m_deep_image_nums
                              << ", need_buf_len=" << m_need_buf_len;
}

int SingleCamera::make_images(char* buf, size_t buf_size) {
    // 检查缓冲区大小是否足够
    if (buf_size < m_need_buf_len) {
        SYLAR_LOG_WARN(g_logger) << "[" << TAG << "] " << "make_images: buf_size(" << buf_size 
                                  << ") < need_buf_len(" << m_need_buf_len << ")";
        return 1;  // buf 太小
    }
    
    char* ptr = buf;
    
    // 写入 RGB 图片
    for (int i = 0; i < m_rgb_image_nums; ++i) {
        // 设置 image_header
        image_header* header = reinterpret_cast<image_header*>(ptr);
        header->image_size = m_rgb_image_size;
        header->type = ImageType::PNG;  // RGB 图片默认为 PNG
        ptr += sizeof(image_header);
        
        // 拷贝图片数据
        if (m_rgb_image && m_rgb_image_size > 0) {
            std::memcpy(ptr, m_rgb_image.get(), m_rgb_image_size);
            ptr += m_rgb_image_size;
        }
    }
    
    // 写入 Deep 图片
    for (int i = 0; i < m_deep_image_nums; ++i) {
        // 设置 image_header
        image_header* header = reinterpret_cast<image_header*>(ptr);
        header->image_size = m_deep_image_size;
        header->type = ImageType::DEEP;  // Deep 图片使用 DEEP 类型
        ptr += sizeof(image_header);
        
        // 拷贝图片数据
        if (m_deep_image && m_deep_image_size > 0) {
            std::memcpy(ptr, m_deep_image.get(), m_deep_image_size);
            ptr += m_deep_image_size;
        }
    }
    
    SYLAR_LOG_DEBUG(g_logger) << "[" << TAG << "] " << "make_images: successfully wrote " 
                               << (m_rgb_image_nums + m_deep_image_nums) << " images, "
                               << "total bytes: " << (ptr - buf);
    
    return 0;  // 成功
}

// ====================================================================================
// NaviCamera Implementation
// ====================================================================================

NaviCamera::NaviCamera(const std::string& dir)
    : m_index(0)
    , m_max_buf_len(0) {

    namespace fs = std::filesystem;

    // 扫描目录下所有 .jpg 文件
    std::vector<std::pair<int, std::string>> numbered_files;
    try {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            const auto& path = entry.path();
            if (path.extension() != ".jpg" && path.extension() != ".JPG" &&
                path.extension() != ".jpeg" && path.extension() != ".JPEG") continue;

            // 尝试将文件名（去掉扩展名）解析为整数，用于数字排序
            std::string stem = path.stem().string();
            try {
                int num = std::stoi(stem);
                numbered_files.emplace_back(num, path.string());
            } catch (...) {
                // 非纯数字文件名：放到最后，按字典序
                numbered_files.emplace_back(INT_MAX, path.string());
            }
        }
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "[NaviCamera] failed to scan directory: " 
                                   << dir << " : " << e.what();
    }

    // 按数字（或字典序）排序
    std::sort(numbered_files.begin(), numbered_files.end(),
              [](const auto& a, const auto& b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second < b.second;
              });

    // 提取路径列表，同时预扫描最大文件大小
    size_t max_image_size = 0;
    for (const auto& [num, fpath] : numbered_files) {
        m_files.push_back(fpath);
        try {
            size_t fsz = static_cast<size_t>(fs::file_size(fpath));
            if (fsz > max_image_size) max_image_size = fsz;
        } catch (...) {}
    }

    m_max_buf_len = sizeof(image_header) + max_image_size;

    SYLAR_LOG_INFO(g_logger) << "[NaviCamera] scanned dir=" << dir
                              << ", found=" << m_files.size() << " images"
                              << ", max_image_size=" << max_image_size
                              << ", buf_len=" << m_max_buf_len;
}

int NaviCamera::make_images(char* buf, size_t buf_size) {
    if (m_files.empty()) {
        SYLAR_LOG_WARN(g_logger) << "[NaviCamera] no images available";
        return 1;
    }

    // 原子地取出当前序号并递增（循环）
    size_t idx = m_index.fetch_add(1, std::memory_order_relaxed) % m_files.size();
    const std::string& fpath = m_files[idx];

    // 读取图片文件
    std::ifstream ifs(fpath, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        SYLAR_LOG_ERROR(g_logger) << "[NaviCamera] cannot open image: " << fpath;
        return 1;
    }
    size_t image_size = static_cast<size_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);

    if (buf_size < sizeof(image_header) + image_size) {
        SYLAR_LOG_WARN(g_logger) << "[NaviCamera] buf_size(" << buf_size
                                  << ") too small for image_size(" << image_size << ")";
        return 1;
    }

    // 写入 image_header
    image_header* header = reinterpret_cast<image_header*>(buf);
    header->image_size = image_size;
    header->type = ImageType::JPG;

    // 写入图片数据
    if (!ifs.read(buf + sizeof(image_header), static_cast<std::streamsize>(image_size))) {
        SYLAR_LOG_ERROR(g_logger) << "[NaviCamera] failed to read image: " << fpath;
        return 1;
    }

    SYLAR_LOG_DEBUG(g_logger) << "[NaviCamera] make_images idx=" << idx
                               << " file=" << fpath
                               << " size=" << image_size;
    return 0;
}

} // namespace device
} // namespace sherry