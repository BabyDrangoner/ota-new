#include "sherry/device/device_camera.h"
#include "sherry/log.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <cassert>

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 测试用的临时图片文件路径
static const std::string TEST_RGB_IMAGE_PATH = "/root/xxl/workspace/ota-new/file/ota_1_1.0.01_gps.jpg";
static const std::string TEST_DEEP_IMAGE_PATH = "/root/xxl/workspace/ota-new/file/ota_1_1.0.01_gps.jpg";

// 创建测试用的模拟图片文件
void create_test_image_files() {
    SYLAR_LOG_INFO(g_logger) << "Creating test image files...";
    
    // 创建模拟 RGB 图片文件 (PNG 文件头 + 一些测试数据)
    {
        std::ofstream rgb_file(TEST_RGB_IMAGE_PATH, std::ios::binary);
        if (rgb_file.is_open()) {
            // PNG 文件头标识
            unsigned char png_header[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
            rgb_file.write(reinterpret_cast<char*>(png_header), sizeof(png_header));
            // 添加一些测试数据
            for (int i = 0; i < 100; ++i) {
                unsigned char data = static_cast<unsigned char>(i % 256);
                rgb_file.write(reinterpret_cast<char*>(&data), 1);
            }
            rgb_file.close();
            SYLAR_LOG_INFO(g_logger) << "Created test RGB image: " << TEST_RGB_IMAGE_PATH;
        } else {
            SYLAR_LOG_ERROR(g_logger) << "Failed to create test RGB image file";
        }
    }
    
    // 创建模拟 Deep 图片文件
    {
        std::ofstream deep_file(TEST_DEEP_IMAGE_PATH, std::ios::binary);
        if (deep_file.is_open()) {
            // 模拟深度数据 (16-bit 深度值)
            for (int i = 0; i < 50; ++i) {
                uint16_t depth_value = static_cast<uint16_t>(i * 100);
                deep_file.write(reinterpret_cast<char*>(&depth_value), sizeof(depth_value));
            }
            deep_file.close();
            SYLAR_LOG_INFO(g_logger) << "Created test Deep image: " << TEST_DEEP_IMAGE_PATH;
        } else {
            SYLAR_LOG_ERROR(g_logger) << "Failed to create test Deep image file";
        }
    }
}

// 清理测试文件
void cleanup_test_files() {
    SYLAR_LOG_INFO(g_logger) << "Cleaning up test files...";
    std::remove(TEST_RGB_IMAGE_PATH.c_str());
    std::remove(TEST_DEEP_IMAGE_PATH.c_str());
}

// ===============================
// Test 1: 基本构造函数测试
// ===============================
void test_single_camera_constructor() {
    SYLAR_LOG_INFO(g_logger) << "========== Test 1: SingleCamera Constructor ==========";
    
    // 创建相机对象
    auto camera = std::make_shared<sherry::device::SingleCamera>(
        2, 1, 
        TEST_RGB_IMAGE_PATH, 
        TEST_DEEP_IMAGE_PATH
    );
    
    // 检查图片数量
    int total_images = camera->get_image_nums();
    SYLAR_LOG_INFO(g_logger) << "Total images: " << total_images;
    assert(total_images == 3 && "Image count should be 3 (2 RGB + 1 Deep)");
    
    // 检查缓冲区大小
    size_t buf_len = camera->get_buf_len();
    SYLAR_LOG_INFO(g_logger) << "Required buffer length: " << buf_len;
    assert(buf_len > 0 && "Buffer length should be greater than 0");
    
    SYLAR_LOG_INFO(g_logger) << "Test 1 PASSED!";
}

// ===============================
// Test 2: make_images 缓冲区太小测试
// ===============================
void test_make_images_buffer_too_small() {
    SYLAR_LOG_INFO(g_logger) << "========== Test 2: make_images Buffer Too Small ==========";
    
    auto camera = std::make_shared<sherry::device::SingleCamera>(
        1, 1, 
        TEST_RGB_IMAGE_PATH, 
        TEST_DEEP_IMAGE_PATH
    );
    
    size_t required_size = camera->get_buf_len();
    SYLAR_LOG_INFO(g_logger) << "Required buffer size: " << required_size;
    
    // 分配一个比需要小的缓冲区
    size_t small_buf_size = required_size / 2;
    char* small_buf = new char[small_buf_size];
    
    int result = camera->make_images(small_buf, small_buf_size);
    SYLAR_LOG_INFO(g_logger) << "make_images with small buffer returned: " << result;
    assert(result == 1 && "Should return 1 when buffer is too small");
    
    delete[] small_buf;
    SYLAR_LOG_INFO(g_logger) << "Test 2 PASSED!";
}

// ===============================
// Test 3: make_images 正常工作测试
// ===============================
void test_make_images_success() {
    SYLAR_LOG_INFO(g_logger) << "========== Test 3: make_images Success ==========";
    
    int rgb_nums = 2;
    int deep_nums = 1;
    
    auto camera = std::make_shared<sherry::device::SingleCamera>(
        rgb_nums, deep_nums, 
        TEST_RGB_IMAGE_PATH, 
        TEST_DEEP_IMAGE_PATH
    );
    
    size_t buf_size = camera->get_buf_len();
    SYLAR_LOG_INFO(g_logger) << "Allocating buffer of size: " << buf_size;
    
    char* buf = new char[buf_size];
    std::memset(buf, 0, buf_size);
    
    int result = camera->make_images(buf, buf_size);
    SYLAR_LOG_INFO(g_logger) << "make_images returned: " << result;
    assert(result == 0 && "Should return 0 on success");
    
    // 验证 image_header 结构
    char* ptr = buf;
    
    // 验证 RGB 图片
    for (int i = 0; i < rgb_nums; ++i) {
        auto* header = reinterpret_cast<sherry::device::SingleCamera::image_header*>(ptr);
        SYLAR_LOG_INFO(g_logger) << "RGB Image " << i << ": size=" << header->image_size 
                                  << ", type=" << static_cast<int>(header->type);
        assert(header->image_size > 0 && "RGB image size should be > 0");
        assert(header->type == sherry::device::ImageType::PNG && "RGB image type should be PNG");
        ptr += sizeof(sherry::device::SingleCamera::image_header) + header->image_size;
    }
    
    // 验证 Deep 图片
    for (int i = 0; i < deep_nums; ++i) {
        auto* header = reinterpret_cast<sherry::device::SingleCamera::image_header*>(ptr);
        SYLAR_LOG_INFO(g_logger) << "Deep Image " << i << ": size=" << header->image_size 
                                  << ", type=" << static_cast<int>(header->type);
        assert(header->image_size > 0 && "Deep image size should be > 0");
        assert(header->type == sherry::device::ImageType::DEEP && "Deep image type should be DEEP");
        ptr += sizeof(sherry::device::SingleCamera::image_header) + header->image_size;
    }
    
    delete[] buf;
    SYLAR_LOG_INFO(g_logger) << "Test 3 PASSED!";
}

// ===============================
// Test 4: 空图片路径测试
// ===============================
void test_empty_image_path() {
    SYLAR_LOG_INFO(g_logger) << "========== Test 4: Empty Image Path ==========";
    
    // 只有 RGB，无 Deep
    auto camera1 = std::make_shared<sherry::device::SingleCamera>(
        1, 0, 
        TEST_RGB_IMAGE_PATH, 
        ""
    );
    
    SYLAR_LOG_INFO(g_logger) << "Camera1 (RGB only): image_nums=" << camera1->get_image_nums()
                              << ", buf_len=" << camera1->get_buf_len();
    assert(camera1->get_image_nums() == 1);
    
    // 只有 Deep，无 RGB
    auto camera2 = std::make_shared<sherry::device::SingleCamera>(
        0, 2, 
        "", 
        TEST_DEEP_IMAGE_PATH
    );
    
    SYLAR_LOG_INFO(g_logger) << "Camera2 (Deep only): image_nums=" << camera2->get_image_nums()
                              << ", buf_len=" << camera2->get_buf_len();
    assert(camera2->get_image_nums() == 2);
    
    // 全空
    auto camera3 = std::make_shared<sherry::device::SingleCamera>(0, 0, "", "");
    SYLAR_LOG_INFO(g_logger) << "Camera3 (Empty): image_nums=" << camera3->get_image_nums()
                              << ", buf_len=" << camera3->get_buf_len();
    assert(camera3->get_image_nums() == 0);
    assert(camera3->get_buf_len() == 0);
    
    SYLAR_LOG_INFO(g_logger) << "Test 4 PASSED!";
}

// ===============================
// Test 5: 图片数据完整性测试
// ===============================
void test_image_data_integrity() {
    SYLAR_LOG_INFO(g_logger) << "========== Test 5: Image Data Integrity ==========";
    
    auto camera = std::make_shared<sherry::device::SingleCamera>(
        1, 0, 
        TEST_RGB_IMAGE_PATH, 
        ""
    );
    
    size_t buf_size = camera->get_buf_len();
    char* buf = new char[buf_size];
    
    int result = camera->make_images(buf, buf_size);
    assert(result == 0);
    
    // 读取原始文件进行比较
    std::ifstream orig_file(TEST_RGB_IMAGE_PATH, std::ios::binary | std::ios::ate);
    size_t orig_size = static_cast<size_t>(orig_file.tellg());
    orig_file.seekg(0, std::ios::beg);
    
    char* orig_data = new char[orig_size];
    orig_file.read(orig_data, orig_size);
    orig_file.close();
    
    // 跳过 header，比较图片数据
    char* image_data = buf + sizeof(sherry::device::SingleCamera::image_header);
    
    bool data_match = (std::memcmp(image_data, orig_data, orig_size) == 0);
    SYLAR_LOG_INFO(g_logger) << "Image data integrity check: " << (data_match ? "PASSED" : "FAILED");
    assert(data_match && "Image data should match original file");
    
    delete[] buf;
    delete[] orig_data;
    
    SYLAR_LOG_INFO(g_logger) << "Test 5 PASSED!";
}

int main(int argc, char** argv) {
    SYLAR_LOG_INFO(g_logger) << "========================================";
    SYLAR_LOG_INFO(g_logger) << "   Device Camera Unit Tests Starting   ";
    SYLAR_LOG_INFO(g_logger) << "========================================";
    
    // 创建测试用的图片文件
    create_test_image_files();
    
    try {
        test_single_camera_constructor();
        test_make_images_buffer_too_small();
        test_make_images_success();
        test_empty_image_path();
        test_image_data_integrity();
        
        SYLAR_LOG_INFO(g_logger) << "========================================";
        SYLAR_LOG_INFO(g_logger) << "   All Tests PASSED!                   ";
        SYLAR_LOG_INFO(g_logger) << "========================================";
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "Test failed with exception: " << e.what();
        cleanup_test_files();
        return 1;
    }
    
    // 清理测试文件
    cleanup_test_files();
    
    return 0;
}
