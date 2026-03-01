#ifndef _SHERRY_DEVICE_CAMERA_H__
#define _SHERRY_DEVICE_CAMERA_H__

#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

namespace sherry{
namespace device{

enum ImageType{
    PNG = 0,
    JPG,
    DEEP,
};

class Camera{
public:
    typedef std::shared_ptr<Camera> ptr;
    virtual int make_images(char* buf, size_t buf_size) = 0; 
    virtual size_t get_buf_len() = 0;
    virtual int get_image_nums() = 0;
};

class SingleCamera: public Camera{
public:
    typedef std::shared_ptr<SingleCamera> ptr;
    SingleCamera(int rgb_nums, int deep_nums, 
                 const std::string& rgb_image_path = "",
                 const std::string& deep_image_path = "");
    /*
        返回值: 
            - 0 成功
            - 1 buf太小
    */
    virtual int make_images(char* buf, size_t buf_size) override;
    virtual size_t get_buf_len() override { return m_need_buf_len;}
    virtual int get_image_nums() override { return m_rgb_image_nums + m_deep_image_nums;}
public:
    struct image_header{
        size_t image_size{0};
        ImageType type{PNG};
    };
private:
    int m_rgb_image_nums;
    int m_deep_image_nums;
    size_t m_need_buf_len;
    std::shared_ptr<char[]> m_rgb_image;   // todo 注册删除器
    size_t m_rgb_image_size;
    std::shared_ptr<char[]> m_deep_image;
    size_t m_deep_image_size;
};
/**
 * @brief 循环访问目录下所有图片的相机
 * 
 * 构造时扫描指定目录下所有 .jpg 文件（按数字文件名排序），
 * 每次调用 make_images() 返回下一张图片，到达末尾后循环回头。
 * 线程安全（多线程调用 make_images 时索引自增互斥）。
 */
class NaviCamera : public Camera {
public:
    typedef std::shared_ptr<NaviCamera> ptr;

    /**
     * @param dir  图片目录路径，扫描其中所有 .jpg 文件
     */
    explicit NaviCamera(const std::string& dir);

    virtual int    make_images(char* buf, size_t buf_size) override;
    virtual size_t get_buf_len() override { return m_max_buf_len; }
    virtual int    get_image_nums() override { return 1; }

    /// 返回扫描到的图片总数
    size_t total() const { return m_files.size(); }
    /// 返回当前即将使用的图片索引（0-based，循环）
    size_t currentIndex() const { return m_index.load(std::memory_order_relaxed) % m_files.size(); }

private:
    std::vector<std::string> m_files;      ///< 排序后的图片路径列表
    std::atomic<size_t>      m_index{0};   ///< 当前序号（循环递增）
    size_t                   m_max_buf_len{0}; ///< sizeof(image_header) + 最大图片字节数
    std::mutex               m_mutex;      ///< 保护 m_index 的互斥锁（可选，atomic 已足够）

    using image_header = SingleCamera::image_header;
};

} // namespace device
} // namespace sherry
#endif