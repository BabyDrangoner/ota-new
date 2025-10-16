#include "http_buffer.h"
#include "../log.h"

namespace sherry{

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

bool HttpBuffer::write(const char* buffer, size_t len){
    MutexType::Lock lock(m_mutex);

    if(m_start_idx <= m_end_idx){
        
        if(m_capacity - m_size < len){
            return false;
        }
        size_t left_available_size = m_start_idx, right_available_size = (m_capacity + 1) - m_end_idx;

        if(len <= right_available_size){
            memcpy(m_buffer + m_end_idx, buffer, len);
            m_end_idx = (m_end_idx + len) % (m_capacity + 1); 
            m_size += len;
            return true;
        } 
        memcpy(m_buffer + m_end_idx, buffer, right_available_size);
        memcpy(m_buffer, buffer + right_available_size, left_available_size);
        
        m_end_idx = (m_end_idx + len) % (m_capacity + 1);
        m_size += len;
        return true;
    } else {
        size_t available_size = m_start_idx - m_end_idx;
        if(available_size < len){
            return false;
        }
        memcpy(m_buffer + m_end_idx, buffer, len);
        m_end_idx = (m_end_idx + len) % (m_capacity + 1); 
        m_size += len;
        return true;
    }

}

std::shared_ptr<char[]> HttpBuffer::read_by_size(size_t size){
    MutexType::Lock lock(m_mutex);

    std::shared_ptr<char[]> ret(new char[size], [](char* p){ delete[] p; });
    if(m_start_idx <= m_end_idx){
        if(m_end_idx - m_start_idx < (int)size){
            return nullptr;
        }
        memcpy(ret.get(), m_buffer + m_start_idx, size);
        m_start_idx += size;
        m_start_idx %= (m_capacity + 1);
        m_size -= size;
    } else{
        size_t left_size = m_end_idx, right_size = (m_capacity + 1) - m_start_idx;
        size_t cur_size = left_size + right_size;
        if(cur_size < size){
            return nullptr;
        }
        memcpy(ret.get(), m_buffer + m_start_idx, right_size);
        memcpy(ret.get() + right_size, m_buffer, left_size);
        m_start_idx += size;
        m_start_idx %= (m_capacity + 1);
        m_size -= size;
    }

    return ret;
}

std::shared_ptr<char[]> HttpBuffer::read_by_end(int idx){
    MutexType::Lock lock(m_mutex);

    idx = idx - 1;
    if(idx < 0){
        idx = (m_capacity + 1) - 1;
    }

    if(m_start_idx <= m_end_idx){
        if(idx < m_start_idx || idx >= m_end_idx){
            SYLAR_LOG_DEBUG(g_logger) << "idx = " << idx
                                      << ", start_idx = " << m_start_idx
                                      << ", m_end_idx = " << m_end_idx;
            return nullptr;
        }
        size_t size = idx - m_start_idx + 1;
        std::shared_ptr<char[]> ret(new char[size], [](char* p){ delete[] p; });
        memcpy(ret.get(), m_buffer + m_start_idx, size);
        m_start_idx += size;
        m_start_idx %= (m_capacity + 1);
        m_size -= size;
        return ret;

    } else {
        if(idx >= m_end_idx && idx < m_start_idx){
            SYLAR_LOG_DEBUG(g_logger) << "idx - 1 = " << idx - 1
                                      << ", start_idx = " << m_start_idx
                                      << ", m_end_idx = " << m_end_idx;
            return nullptr;
        }
        if(idx >= m_start_idx){
            size_t size = idx - m_start_idx + 1;
            std::shared_ptr<char[]> ret(new char[size], [](char* p){ delete[] p; });
            memcpy(ret.get(), m_buffer + m_start_idx, size);
            m_start_idx += size;
            m_start_idx %= (m_capacity + 1);
            m_size -= size;
            return ret;
        } else {
            int right_size = (m_capacity + 1) - m_start_idx, left_size = idx + 1;
            size_t size = right_size + left_size;
            std::shared_ptr<char[]> ret(new char[size], [](char* p){ delete[] p; });
            memcpy(ret.get(), m_buffer + m_start_idx, right_size);
            memcpy(ret.get() + right_size, m_buffer, left_size);
            m_start_idx += size;
            m_start_idx %= (m_capacity + 1);
            m_size -= size;
            return ret;
        }
    }

}

std::shared_ptr<char[]> HttpBuffer::read_by_zero(){
    MutexType::Lock lock(m_mutex);

    size_t size = 0;
    int idx = m_start_idx;
    while(idx != m_end_idx){
        if(m_buffer[idx] == '\0') break;
        ++size;
        idx = (idx + 1) % (m_capacity + 1);
    }

    std::shared_ptr<char[]> ret(new char[size + 1], [](char* p){ delete[] p; });

    idx = m_start_idx;
    for(int i = 0;i < (int)size;++i){
        ret[i] = m_buffer[idx];
        idx = (idx + 1) % (m_capacity + 1);
    }

    m_start_idx += size + 1;
    m_size -= size;
    ret[size] = '\0';
    return ret;

}

bool HttpBuffer::next(char& val, int& idx){
    MutexType::Lock lock(m_mutex);

    if(m_start_idx == m_end_idx){
        return false;
    } else if(m_start_idx < m_end_idx){
        if(idx < m_start_idx || idx >= m_end_idx){
            return false;
        }
        ++idx;
        val = m_buffer[idx];
        return true;
    } else {
        if(idx >= m_end_idx || idx < m_start_idx){
            return false;
        }
        idx = (idx + 1) % (m_capacity + 1);
        val = m_buffer[idx];
        return true;
    }

}

size_t HttpBuffer::get_available_size() {
    MutexType::Lock lock(m_mutex);

    if(m_start_idx <= m_end_idx){
        return m_capacity + 1 - m_start_idx + m_end_idx;
    } else {
        
        return m_start_idx - m_end_idx;
    }
}

size_t HttpBuffer::get_data_size() {
    return m_size;
}

int HttpBuffer::get_start_idx(){ 
    MutexType::Lock lock(m_mutex);
    return m_start_idx;
}

int HttpBuffer::get_end_idx(){ 
    MutexType::Lock lock(m_mutex);
    return m_end_idx;
}

char HttpBuffer::get_val(int idx){ 
    MutexType::Lock lock(m_mutex);
    return m_buffer[idx];
}

int HttpBuffer::get_last_idx() {
    MutexType::Lock lock(m_mutex);

    SYLAR_ASSERT(m_end_idx != m_start_idx);
    if(m_end_idx - 1 < 0){
        return m_capacity;
    } else {
        return m_end_idx - 1;
    }
}

void HttpBuffer::print_infos(){
    std::cout << "--------------" << std::endl;
    std::cout << "m_capacity = " << m_capacity << std::endl;
    std::cout << "m_size = " << m_size << std::endl;
    std::cout << "m_start_idx = " << m_start_idx << std::endl;
    std::cout << "m_end_idx = " << m_end_idx << std::endl;
    std::cout << std::string(m_buffer) << std::endl;
    std::cout << std::endl;

}

int HttpBuffer::next_idx(int idx) {
    MutexType::Lock lock(m_mutex);
    return (idx + 1) % m_capacity;
}

int HttpBuffer::pre_idx(int idx) {
    MutexType::Lock lock(m_mutex);
    if(idx - 1 < 0){
        return m_capacity - 1;
    }
    return idx - 1;
}



}