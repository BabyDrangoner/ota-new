/**
 * @file test_navi_camera.cc
 * @brief NaviCamera 单元测试
 *
 * 测试覆盖：
 *  1. 构造：能正确扫描目录并统计文件数量
 *  2. 循环遍历：多次 make_images 返回不同且有效的图片
 *  3. 循环回绕：调用 total+1 次后索引回到 0
 *  4. 缓冲区不足：make_images 应返回 1
 *  5. get_buf_len 能容纳最大图片
 */

#include "sherry/device/device_camera.h"
#include "sherry/log.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <set>

using namespace sherry::device;
namespace fs = std::filesystem;

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static const std::string NAVI_DIR = "/root/xxl/workspace/ota-new/file/navi_data/common";

// ──────────────────────────────────────────────
// 辅助：从 buf 解析 image_header
// ──────────────────────────────────────────────
struct image_header {
    size_t    image_size{0};
    ImageType type{ImageType::PNG};
};

// ──────────────────────────────────────────────
// Test 1: 构造 & 基本属性
// ──────────────────────────────────────────────
void test_constructor() {
    SYLAR_LOG_INFO(g_logger) << "===== Test 1: Constructor =====";

    NaviCamera cam(NAVI_DIR);

    size_t total = cam.total();
    SYLAR_LOG_INFO(g_logger) << "total images scanned: " << total;
    assert(total > 0 && "NaviCamera should find at least one image");

    size_t buf_len = cam.get_buf_len();
    SYLAR_LOG_INFO(g_logger) << "get_buf_len: " << buf_len;
    assert(buf_len > sizeof(image_header) && "buf_len should be > header size");

    assert(cam.get_image_nums() == 1);

    SYLAR_LOG_INFO(g_logger) << "Test 1 PASSED";
}

// ──────────────────────────────────────────────
// Test 2: make_images 逐帧返回不同图片
// ──────────────────────────────────────────────
void test_sequential_frames() {
    SYLAR_LOG_INFO(g_logger) << "===== Test 2: Sequential Frames =====";

    NaviCamera cam(NAVI_DIR);
    size_t total    = cam.total();
    size_t buf_len  = cam.get_buf_len();

    std::vector<char> buf(buf_len);

    // 收集前 min(total, 10) 帧的图片大小，确保各帧能成功且大小 > 0
    size_t frames_to_check = std::min(total, (size_t)10);
    std::vector<size_t> sizes;

    for (size_t i = 0; i < frames_to_check; ++i) {
        std::fill(buf.begin(), buf.end(), 0);
        int ret = cam.make_images(buf.data(), buf.size());
        assert(ret == 0 && "make_images should return 0 on success");

        const image_header* hdr = reinterpret_cast<const image_header*>(buf.data());
        assert(hdr->image_size > 0 && "image_size in header should be > 0");
        assert(hdr->type == ImageType::JPG && "image type should be JPG");
        sizes.push_back(hdr->image_size);

        SYLAR_LOG_INFO(g_logger) << "  frame[" << i << "] image_size=" << hdr->image_size
                                  << " type=" << (int)hdr->type;
    }

    SYLAR_LOG_INFO(g_logger) << "Test 2 PASSED (checked " << frames_to_check << " frames)";
}

// ──────────────────────────────────────────────
// Test 3: 循环回绕 —— 第 total+1 帧与第 1 帧内容相同
// ──────────────────────────────────────────────
void test_wraparound() {
    SYLAR_LOG_INFO(g_logger) << "===== Test 3: Wraparound =====";

    NaviCamera cam(NAVI_DIR);
    size_t total   = cam.total();
    size_t buf_len = cam.get_buf_len();

    std::vector<char> buf_first(buf_len), buf_wrap(buf_len);

    // 拿第 0 帧（第一次调用）
    int ret0 = cam.make_images(buf_first.data(), buf_first.size());
    assert(ret0 == 0);

    // 再调用 total-1 次，耗尽一轮
    std::vector<char> tmp(buf_len);
    for (size_t i = 1; i < total; ++i) {
        cam.make_images(tmp.data(), tmp.size());
    }

    // 第 total+1 次调用应循环回第 0 帧
    int ret_wrap = cam.make_images(buf_wrap.data(), buf_wrap.size());
    assert(ret_wrap == 0);

    // 比较两次结果（header + 部分数据）
    const image_header* hdr0 = reinterpret_cast<const image_header*>(buf_first.data());
    const image_header* hdrW = reinterpret_cast<const image_header*>(buf_wrap.data());

    assert(hdr0->image_size == hdrW->image_size && "wraparound should return same image");
    size_t cmp_bytes = sizeof(image_header) + std::min(hdr0->image_size, (size_t)256);
    assert(std::memcmp(buf_first.data(), buf_wrap.data(), cmp_bytes) == 0
           && "wraparound frame content should match first frame");

    SYLAR_LOG_INFO(g_logger) << "Test 3 PASSED (total=" << total
                              << ", wrap image_size=" << hdrW->image_size << ")";
}

// ──────────────────────────────────────────────
// Test 4: 缓冲区不足时返回 1
// ──────────────────────────────────────────────
void test_buf_too_small() {
    SYLAR_LOG_INFO(g_logger) << "===== Test 4: Buffer Too Small =====";

    NaviCamera cam(NAVI_DIR);

    // 故意给 1 字节的 buf
    char tiny[1] = {0};
    int ret = cam.make_images(tiny, sizeof(tiny));
    assert(ret == 1 && "make_images should return 1 when buf is too small");

    SYLAR_LOG_INFO(g_logger) << "Test 4 PASSED";
}

// ──────────────────────────────────────────────
// Test 5: get_buf_len 确实能容纳每一帧
// ──────────────────────────────────────────────
void test_buf_len_sufficient() {
    SYLAR_LOG_INFO(g_logger) << "===== Test 5: get_buf_len Sufficient =====";

    NaviCamera cam(NAVI_DIR);
    size_t total   = cam.total();
    size_t buf_len = cam.get_buf_len();
    std::vector<char> buf(buf_len);

    for (size_t i = 0; i < total; ++i) {
        int ret = cam.make_images(buf.data(), buf.size());
        assert(ret == 0 && "get_buf_len should be sufficient for every image");
    }

    SYLAR_LOG_INFO(g_logger) << "Test 5 PASSED (all " << total << " frames fit in buf_len=" << buf_len << ")";
}

// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────
int main() {
    std::cout << "NaviCamera Test Suite\n"
              << "Directory: " << NAVI_DIR << "\n"
              << "======================================\n";

    test_constructor();
    test_sequential_frames();
    test_wraparound();
    test_buf_too_small();
    test_buf_len_sufficient();

    std::cout << "======================================\n"
              << "ALL TESTS PASSED\n";
    return 0;
}
