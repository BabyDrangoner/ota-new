#include "icp_rx_slot.h"
#include <algorithm>

namespace sherry {
namespace icp {

//------------------------------------------------------------------------------
// RxSlot
//------------------------------------------------------------------------------

RxSlot::RxSlot(size_t max_size)
    : m_maxSize(max_size)
    , m_buffer(new uint8_t[max_size]) {
    std::memset(m_buffer, 0, max_size);
}

RxSlot::~RxSlot() {
    delete[] m_buffer;
    m_buffer = nullptr;
}

bool RxSlot::write(const uint8_t* data, size_t len, uint64_t seq, uint64_t timestamp_ms) {
    if (len > m_maxSize || data == nullptr) {
        return false;
    }
    
    // 版本号+1 变为奇数, 表示开始写入
    uint64_t old_version = m_version.load(std::memory_order_relaxed);
    m_version.store(old_version + 1, std::memory_order_release);
    
    // 内存屏障确保版本号变化对其他线程可见
    std::atomic_thread_fence(std::memory_order_seq_cst);
    
    // 复制数据
    std::memcpy(m_buffer, data, len);
    
    // 更新元数据
    m_len.store(len, std::memory_order_relaxed);
    m_seq.store(seq, std::memory_order_relaxed);
    m_timestamp.store(timestamp_ms, std::memory_order_relaxed);
    m_valid.store(true, std::memory_order_relaxed);
    
    // 内存屏障
    std::atomic_thread_fence(std::memory_order_seq_cst);
    
    // 版本号+1 变为偶数, 表示写入完成
    m_version.store(old_version + 2, std::memory_order_release);
    
    return true;
}

bool RxSlot::tryRead(std::vector<uint8_t>& out_data, uint64_t& out_seq,
                     uint64_t& out_ts, int max_retries) const {
    for (int retry = 0; retry < max_retries; ++retry) {
        // 读取版本号
        uint64_t version1 = m_version.load(std::memory_order_acquire);
        
        // 如果是奇数, 表示写入中, 等待
        if (version1 & 1) {
            continue;
        }
        
        // 检查是否有效
        if (!m_valid.load(std::memory_order_relaxed)) {
            return false;
        }
        
        // 内存屏障
        std::atomic_thread_fence(std::memory_order_acquire);
        
        // 读取元数据
        size_t len = m_len.load(std::memory_order_relaxed);
        uint64_t seq = m_seq.load(std::memory_order_relaxed);
        uint64_t ts = m_timestamp.load(std::memory_order_relaxed);
        
        // 读取数据
        out_data.resize(len);
        std::memcpy(out_data.data(), m_buffer, len);
        
        // 内存屏障
        std::atomic_thread_fence(std::memory_order_acquire);
        
        // 再次检查版本号
        uint64_t version2 = m_version.load(std::memory_order_acquire);
        
        // 如果版本号一致且为偶数, 读取成功
        if (version1 == version2) {
            out_seq = seq;
            out_ts = ts;
            return true;
        }
        
        // 版本号不一致, 重试
    }
    
    return false;
}

uint64_t RxSlot::getCurrentSeq() const {
    // 使用宽松语义快速读取, 仅用于判断是否有新数据
    if (!m_valid.load(std::memory_order_relaxed)) {
        return 0;
    }
    return m_seq.load(std::memory_order_acquire);
}

uint64_t RxSlot::getCurrentTimestamp() const {
    return m_timestamp.load(std::memory_order_acquire);
}

bool RxSlot::isValid() const {
    return m_valid.load(std::memory_order_acquire);
}

void RxSlot::invalidate() {
    // 版本号+1 变为奇数
    uint64_t old_version = m_version.load(std::memory_order_relaxed);
    m_version.store(old_version + 1, std::memory_order_release);
    
    std::atomic_thread_fence(std::memory_order_seq_cst);
    
    m_valid.store(false, std::memory_order_relaxed);
    m_seq.store(0, std::memory_order_relaxed);
    m_timestamp.store(0, std::memory_order_relaxed);
    m_len.store(0, std::memory_order_relaxed);
    
    std::atomic_thread_fence(std::memory_order_seq_cst);
    
    // 版本号+1 变为偶数
    m_version.store(old_version + 2, std::memory_order_release);
}

//------------------------------------------------------------------------------
// RxSlotManager
//------------------------------------------------------------------------------

RxSlotManager::RxSlotManager(size_t max_cars, size_t slot_size)
    : m_maxCars(max_cars)
    , m_slotSize(slot_size) {
    m_slots.reserve(max_cars);
    for (size_t i = 0; i < max_cars; ++i) {
        m_slots.push_back(std::make_shared<RxSlot>(slot_size));
    }
}

RxSlot* RxSlotManager::getSlot(uint32_t car_id) {
    if (car_id >= m_maxCars) {
        return nullptr;
    }
    return m_slots[car_id].get();
}

} // namespace icp
} // namespace sherry
