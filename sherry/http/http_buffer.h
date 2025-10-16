#ifndef _SHERRY_HTTP_BUFFER_H__
#define _SHERRY_HTTP_BUFFER_H__

#include "../sherry.h"

namespace sherry{

class HttpBuffer : public Noncopyable{
public:
    typedef std::shared_ptr<HttpBuffer> ptr;
    typedef Mutex MutexType;

    HttpBuffer(size_t capacity)
        :m_capacity(capacity)
        ,m_size(0)
        ,m_start_idx(0)
        ,m_end_idx(0){
        m_buffer = new char[capacity + 1];
    }

    ~HttpBuffer(){
        delete []m_buffer;
    }

    void reset(){
        m_size = 0;
        m_start_idx = 0;
        m_end_idx = 0;
    }

    bool empty(){
        return m_start_idx == m_end_idx;
    }

    bool write(const char* buffer, size_t len);
    std::shared_ptr<char[]> read_by_size(size_t size);
    std::shared_ptr<char[]> read_by_end(int idx);
    std::shared_ptr<char[]> read_by_zero();

    bool next(char& val, int& idx);
    int next_idx(int idx);
    int pre_idx(int idx);

    void set_val(char v, int idx){ m_buffer[idx] = v;}

    int get_start_idx();
    int get_end_idx();

    int get_last_idx() ;
    char get_val(int idx);

    size_t get_available_size();
    size_t get_data_size();

    void print_infos();

private:
    MutexType m_mutex;
    size_t m_capacity;
    std::atomic<size_t> m_size;
    int m_start_idx;
    int m_end_idx;
    char* m_buffer;
};


}

#endif