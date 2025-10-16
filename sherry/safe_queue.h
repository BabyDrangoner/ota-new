#ifndef _SHERRY_SAFEQUEUE_H__
#define _SHERRY_SAFEQUEUE_H__

#include "thread.h"

#include <memory>
#include <queue>

namespace sherry{

template <typename T>
class SafeQueue : public Noncopyable{
public:
    typedef Mutex MutexType;
    typedef std::shared_ptr<queue<T>> ptr;

    void push(T v){
        MutexType::Lock lock(m_mutex);
        m_queue.push(v);
    }

    void pop(){
        MutexType::Lock lock(m_mutex);
        m_queue.pop();
    }

    bool empty(){
        MutexType::Lock lock(m_mutex);
        return m_queue.empty();
    }

    bool pop_and_get(T& val){
        MutexType::Lock lock(m_mutex);
        if(m_queue.empty()){
           return false; 
        }
        val = m_queue.top();
        m_queue.pop();
        return true;
    }

private:
    MutexType m_mutex;
    std::queue<T> m_queue;

};

}

#endif