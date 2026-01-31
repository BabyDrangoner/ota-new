#ifndef _SHERRY_DEVICE_CAMERA_H__
#define _SHERRY_DEVICE_CAMERA_H__

#include <memory>
#include <string>

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
} // namespace device
} // namespace sherry
#endif