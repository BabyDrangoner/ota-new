#ifndef __SHERRY_ICP_RX_SLOT_H__
#define __SHERRY_ICP_RX_SLOT_H__

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include <cstring>

namespace sherry {
namespace icp {

/**
 * @brief 单车单槽覆盖缓存(RxSlot)
 * 
 * 实现 Latest-Only 语义:
 * - IO 线程写入会覆盖旧内容
 * - 控制线程读取时按 seq 判断是否仍是最新
 * 
 * 使用版本号方案实现无锁读写:
 * - IO 写: version++ (odd) → memcpy → version++ (even)
 * - Control 读: 读到同一个偶数 version 前后不变即可认为一致
 */
class RxSlot {
public:
    typedef std::shared_ptr<RxSlot> ptr;
    
    /**
     * @brief 构造函数
     * @param max_size 缓存最大容量
     */
    explicit RxSlot(size_t max_size);
    
    ~RxSlot();
    
    /**
     * @brief 写入数据(覆盖旧数据)
     * @param data 数据指针
     * @param len 数据长度
     * @param seq 序列号
     * @param timestamp_ms 时间戳
     * @return true 如果写入成功, false 如果数据太大
     */
    bool write(const uint8_t* data, size_t len, uint64_t seq, uint64_t timestamp_ms);
    
    /**
     * @brief 尝试读取最新数据(一致性读)
     * @param out_data 输出缓冲区
     * @param out_len 输出长度
     * @param out_seq 输出序列号
     * @param out_ts 输出时间戳  
     * @param max_retries 最大重试次数
     * @return true 如果读取成功且一致, false 如果槽为空或读取不一致
     */
    bool tryRead(std::vector<uint8_t>& out_data, uint64_t& out_seq, 
                 uint64_t& out_ts, int max_retries = 3) const;
    
    /**
     * @brief 获取当前槽的序列号(用于快速判断是否有更新)
     * @return 当前序列号, 如果槽为空返回0
     */
    uint64_t getCurrentSeq() const;
    
    /**
     * @brief 获取当前槽的时间戳
     */
    uint64_t getCurrentTimestamp() const;
    
    /**
     * @brief 检查槽是否有效(有数据)
     */
    bool isValid() const;
    
    /**
     * @brief 使槽失效(清除数据)
     */
    void invalidate();
    
    /**
     * @brief 获取缓冲区最大容量
     */
    size_t getMaxSize() const { return m_maxSize; }
    
    /**
     * @brief 获取当前数据长度
     */
    size_t getLength() const { return m_len.load(std::memory_order_acquire); }
    
private:
    // 版本号: 奇数表示写入中, 偶数表示写入完成
    std::atomic<uint64_t> m_version{0};
    
    // 元数据
    std::atomic<uint64_t> m_seq{0};
    std::atomic<uint64_t> m_timestamp{0};
    std::atomic<size_t> m_len{0};
    std::atomic<bool> m_valid{false};
    
    // 数据缓冲区
    size_t m_maxSize;
    uint8_t* m_buffer;
};

/**
 * @brief RxSlot 管理器
 * 
 * 管理多个车辆的 RxSlot, 按 car_id 索引
 */
class RxSlotManager {
public:
    typedef std::shared_ptr<RxSlotManager> ptr;
    
    /**
     * @brief 构造函数
     * @param max_cars 最大车辆数
     * @param slot_size 每个槽的大小
     */
    RxSlotManager(size_t max_cars, size_t slot_size);
    
    /**
     * @brief 获取或创建指定车辆的 RxSlot
     * @param car_id 车辆ID
     * @return RxSlot指针, 如果car_id超出范围返回nullptr
     */
    RxSlot* getSlot(uint32_t car_id);
    
    /**
     * @brief 获取车辆数
     */
    size_t getCarCount() const { return m_maxCars; }
    
    /**
     * @brief 获取单槽大小
     */
    size_t getSlotSize() const { return m_slotSize; }
    
private:
    size_t m_maxCars;
    size_t m_slotSize;
    std::vector<RxSlot::ptr> m_slots;
};

} // namespace icp
} // namespace sherry

#endif // __SHERRY_ICP_RX_SLOT_H__
