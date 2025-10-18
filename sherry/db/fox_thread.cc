// fox_thread.cc
// 说明: 实现基于 libevent 的线程与线程池调度。通过 socketpair 唤醒事件循环批量执行回调任务。
// 每个 FoxThread 拥有一个 event_base, 外部通过 dispatch 推入任务并写入管道通知。
// 线程池 FoxThreadPool 支持普通轮询与 advance 模式(按空闲线程租借执行)。
// 线程管理器 FoxThreadManager 通过名称统一管理多个线程或线程池。
#include "fox_thread.h"
#include "sherry/config.h"
#include "sherry/log.h"
#include "sherry/util.h"
#include "sherry/macro.h"
#include "sherry/config.h"
#include <iomanip>

namespace sherry {

// 全局日志器
static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 配置: 读取线程/线程池配置集合, key=名称, value=参数映射(num/advance等)
static sherry::ConfigVar<std::map<std::string, std::map<std::string, std::string> > >::ptr g_thread_info_set
        = Config::Lookup("fox_thread", std::map<std::string, std::map<std::string, std::string> >()
            ,"config for thread");

static RWMutex s_thread_mutex;
static std::map<uint64_t, std::string> s_thread_names;

// 线程局部指针, 标记当前执行的 FoxThread 实例
thread_local FoxThread* s_thread = nullptr;

// read_cb: libevent 读事件回调。
// 触发条件: 有外部线程通过 socketpair 写端发送一个字节唤醒本线程。
// 处理逻辑: 交换当前待执行回调列表, 顺序执行, 若遇到空指针标记则结束事件循环。
// 参数:
//   sock  : 读端 fd
//   which : 事件类型标志(未直接使用)
//   args  : FoxThread* 指针
// 线程安全: 使用写锁交换队列后释放锁执行, 减少临界区。
void FoxThread::read_cb(evutil_socket_t sock, short which, void* args) {
    FoxThread* thread = static_cast<FoxThread*>(args);
    uint8_t cmd[4096];
    if(recv(sock, cmd, sizeof(cmd), 0) > 0) {
        std::list<callback> callbacks;
        RWMutex::WriteLock lock(thread->m_mutex);
        callbacks.swap(thread->m_callbacks);
        lock.unlock();
        thread->m_working = true;
        for(auto it = callbacks.begin();
                it != callbacks.end(); ++it) {
            if(*it) {
                //SYLAR_ASSERT(thread == GetThis());
                try {
                    (*it)();
                } catch (std::exception& ex) {
                    SYLAR_LOG_ERROR(g_logger) << "exception:" << ex.what();
                } catch (const char* c) {
                    SYLAR_LOG_ERROR(g_logger) << "exception:" << c;
                } catch (...) {
                    SYLAR_LOG_ERROR(g_logger) << "uncatch exception";
                }
            } else {
                event_base_loopbreak(thread->m_base);
                thread->m_start = false;
                thread->unsetThis();
                break;
            }
        }
        sherry::Atomic::addFetch(thread->m_total, callbacks.size());
        thread->m_working = false;
    }
}

// 构造函数: 初始化 socketpair, 设置非阻塞, 创建/复用 event_base, 注册读事件。
// 参数:
//   name : 线程基础名称
//   base : 可选外部传入的 event_base (复用); 若为空则新建。
// 异常: socketpair 创建失败抛出 logic_error。
FoxThread::FoxThread(const std::string& name, struct event_base* base)
    :m_read(0)
    ,m_write(0)
    ,m_base(NULL)
    ,m_event(NULL)
    ,m_thread(NULL)
    ,m_name(name)
    ,m_working(false)
    ,m_start(false)
    ,m_total(0) {
    int fds[2];
    if(evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        //SYLAR_LOG_ERROR(g_logger) << "FoxThread init error";
        throw std::logic_error("thread init error");
    }

    evutil_make_socket_nonblocking(fds[0]);
    evutil_make_socket_nonblocking(fds[1]);

    m_read = fds[0];
    m_write = fds[1];

    if(base) {
        m_base = base;
        setThis();
    } else {
        m_base = event_base_new();
    }
    m_event = event_new(m_base, m_read, EV_READ | EV_PERSIST, read_cb, this);
    event_add(m_event, NULL);
}

// dump: 输出当前线程状态(名称/是否工作/队列任务数/已执行总数)。
// 用于监控与调试。
void FoxThread::dump(std::ostream& os) {
    RWMutex::ReadLock lock(m_mutex);
    os << "[thread name=" << m_name
       << " working=" << m_working
       << " tasks=" << m_callbacks.size()
       << " total=" << m_total
       << "]" << std::endl;
}

// getId: 返回底层 std::thread 的线程 id, 若未启动返回默认构造 id。
std::thread::id FoxThread::getId() const {
    if(m_thread) {
        return m_thread->get_id();
    }
    return std::thread::id();
}

// getData: 获取线程关联的任意键值数据指针, 未找到返回 nullptr。
// 注意: 未加锁(注释掉的锁), 假设调用场景受外部同步约束。
void* FoxThread::getData(const std::string& name) {
    //Mutex::ReadLock lock(m_mutex);
    auto it = m_datas.find(name);
    return it == m_datas.end() ? nullptr : it->second;
}

// setData: 设置线程关联的任意键值数据。
// 注意: 未加锁(注释掉的锁), 若多线程并发写需补锁。
void  FoxThread::setData(const std::string& name, void* v) {
    //Mutex::WriteLock lock(m_mutex);
    m_datas[name] = v;
}

// 析构: 关闭 fd, 停止线程(插入终止任务), join 等待结束, 释放 event 与 base。
// 注意: join 内部已 delete m_thread, 此处再次判断。
FoxThread::~FoxThread() {
    if(m_read) {
        close(m_read);
    }
    if(m_write) {
        close(m_write);
    }
    stop();
    join();
    if(m_thread) {
        delete m_thread;
    }
    if(m_event) {
        event_free(m_event);
    }
    if(m_base) {
        event_base_free(m_base);
    }
}

// start: 启动线程并进入事件循环(通过 thread_cb)。重复调用抛异常。
void FoxThread::start() {
    if(m_thread) {
        //SYLAR_LOG_ERROR(g_logger) << "FoxThread is running";
        throw std::logic_error("FoxThread is running");
    }

    m_thread = new std::thread(std::bind(&FoxThread::thread_cb, this));
    m_start = true;
}

// thread_cb: 线程入口函数。
// 设置线程名, 执行初始化回调, 进入 libevent 主循环。
void FoxThread::thread_cb() {
    //std::cout << "FoxThread(" << m_name << "," << pthread_self() << ")" << std::endl;
    setThis();
    pthread_setname_np(pthread_self(), m_name.substr(0, 15).c_str());
    if(m_initCb) {
        m_initCb(this);
        m_initCb = nullptr;
    }
    event_base_loop(m_base, 0);
}

// dispatch: 提交单个回调任务。
// 操作: 写锁入队 -> 发送一个字节唤醒事件循环。
// 返回: 发送失败返回 false。
bool FoxThread::dispatch(callback cb) {
    RWMutex::WriteLock lock(m_mutex);
    m_callbacks.push_back(cb);
    //if(m_callbacks.size() > 1) {
    //    std::cout << std::this_thread::get_id() << ":" << m_callbacks.size() << " " << m_name << std::endl;
    //}
    lock.unlock();
    uint8_t cmd = 1;
    //write(m_write, &cmd, sizeof(cmd));
    if(send(m_write, &cmd, sizeof(cmd), 0) <= 0) {
        return false;
    }
    return true;
}

// dispatch(id, cb): 与无 id 版本一致, id 参数目前未使用, 保留接口兼容。
bool FoxThread::dispatch(uint32_t id, callback cb) {
    return dispatch(cb);
}

// batchDispatch: 批量提交任务, 减少多次唤醒开销。入队后仅发送一次通知。
// 返回: 发送失败 false。
bool FoxThread::batchDispatch(const std::vector<callback>& cbs) {
    RWMutex::WriteLock lock(m_mutex);
    for(auto& i : cbs) {
        m_callbacks.push_back(i);
    }
    lock.unlock();
    uint8_t cmd = 1;
    if(send(m_write, &cmd, sizeof(cmd), 0) <= 0) {
        return false;
    }
    return true;
}

// broadcast: 对单线程实现等同 dispatch, 为统一接口保留。
void FoxThread::broadcast(callback cb) {
    dispatch(cb);
}

// stop: 推入空指针任务作为终止标记并唤醒事件循环, 结束后 loopbreak。
void FoxThread::stop() {
    RWMutex::WriteLock lock(m_mutex);
    m_callbacks.push_back(nullptr);
    if(m_thread) {
        uint8_t cmd = 0;
        //write(m_write, &cmd, sizeof(cmd));
        send(m_write, &cmd, sizeof(cmd), 0);
    }
    //if(m_data) {
    //    delete m_data;
    //    m_data = NULL;
    //}
}

// join: 若线程已启动等待其结束并释放线程对象。
void FoxThread::join() {
    if(m_thread) {
        m_thread->join();
        delete m_thread;
        m_thread = NULL;
    }
}

// FoxThreadPool 构造: 创建 size 个 FoxThread。
// advance=true 表示启用“空闲线程调度”模式; false 时使用简单轮询分发。
FoxThreadPool::FoxThreadPool(uint32_t size, const std::string& name, bool advance)
    :m_size(size)
    ,m_cur(0)
    ,m_name(name)
    ,m_advance(advance)
    ,m_start(false)
    ,m_total(0) {
    m_threads.resize(m_size);
    for(size_t i = 0; i < size; ++i) {
        FoxThread* t(new FoxThread(name + "_" + std::to_string(i)));
        m_threads[i] = t;
    }
}

// 析构: 释放所有线程对象(需确保已停止与 join)。
FoxThreadPool::~FoxThreadPool() {
    for(size_t i = 0; i < m_size; ++i) {
        delete m_threads[i];
    }
}

// start: 启动所有线程, 设置初始化回调, 将其标记为空闲列表, 然后尝试调度挂起任务。
void FoxThreadPool::start() {
    for(size_t i = 0; i < m_size; ++i) {
        m_threads[i]->setInitCb(m_initCb);
        m_threads[i]->start();
        m_freeFoxThreads.push_back(m_threads[i]);
    }
    if(m_initCb) {
        m_initCb = nullptr;
    }
    m_start = true;
    check();
}

// stop: 通知所有线程停止(插入终止任务)。
void FoxThreadPool::stop() {
    for(size_t i = 0; i < m_size; ++i) {
        m_threads[i]->stop();
    }
    m_start = false;
}

// join: 等待所有线程结束。
void FoxThreadPool::join() {
    for(size_t i = 0; i < m_size; ++i) {
        m_threads[i]->join();
    }
}

// releaseFoxThread: 线程执行完 wrapcb 后 shared_ptr 释放触发, 将线程归还空闲列表并继续调度。
void FoxThreadPool::releaseFoxThread(FoxThread* t) {
    do {
        RWMutex::WriteLock lock(m_mutex);
        m_freeFoxThreads.push_back(t);
    } while(0);
    check();
}

// dispatch: 提交单任务。
// 非 advance: 直接轮询选择线程调用其 dispatch。
// advance: 任务入池队列等待空闲线程领取。
bool FoxThreadPool::dispatch(callback cb) {
    do {
        sherry::Atomic::addFetch(m_total, (uint64_t)1);
        RWMutex::WriteLock lock(m_mutex);
        if(!m_advance) {
            return m_threads[m_cur++ % m_size]->dispatch(cb);
        }
        m_callbacks.push_back(cb);
    } while(0);
    check();
    return true;
}

// batchDispatch: 批量提交任务。
// 非 advance: 逐一轮询分发。
// advance: 全部推入池队列, 唤醒调度。
bool FoxThreadPool::batchDispatch(const std::vector<callback>& cbs) {
    sherry::Atomic::addFetch(m_total, cbs.size());
    RWMutex::WriteLock lock(m_mutex);
    if(!m_advance) {
        for(auto cb : cbs) {
            m_threads[m_cur++ % m_size]->dispatch(cb);
        }
        return true;
    }
    for(auto cb : cbs) {
        m_callbacks.push_back(cb);
    }
    lock.unlock();
    check();
    return true;
}

// check: 调度核心。
// 条件: 有空闲线程 且 有待执行任务。
// 操作: 取一个空闲线程 + 一个任务, 用 wrapcb 包装后提交到该线程。
// 若线程尚未启动, 任务回退到队列。
void FoxThreadPool::check() {
    do {
        if(!m_start) {
            break;
        }
        RWMutex::WriteLock lock(m_mutex);
        if(m_freeFoxThreads.empty() || m_callbacks.empty()) {
            break;
        }

        std::shared_ptr<FoxThread> thr(m_freeFoxThreads.front(),
                std::bind(&FoxThreadPool::releaseFoxThread,
                    this, std::placeholders::_1));
        m_freeFoxThreads.pop_front();

        callback cb = m_callbacks.front();
        m_callbacks.pop_front();
        lock.unlock();

        if(thr->isStart()) {
            thr->dispatch(std::bind(&FoxThreadPool::wrapcb, this, thr, cb));
        } else {
            RWMutex::WriteLock lock(m_mutex);
            m_callbacks.push_front(cb);
        }
    } while(true);
}

// wrapcb: 真实执行用户回调。shared_ptr 保持线程在执行期间不被归还, 结束后自动释放触发 releaseFoxThread。
void FoxThreadPool::wrapcb(std::shared_ptr<FoxThread> thr, callback cb) {
    cb();
}

// dispatch(id, cb): 指定线程索引分发任务(取模保证合法)。不判断线程是否空闲。
bool FoxThreadPool::dispatch(uint32_t id, callback cb) {
    sherry::Atomic::addFetch(m_total, (uint64_t)1);
    return m_threads[id % m_size]->dispatch(cb);
}

// getRandFoxThread: 简单轮询选择一个线程返回。
FoxThread* FoxThreadPool::getRandFoxThread() {
    return m_threads[m_cur++ % m_size];
}

// broadcast: 向池中所有线程都投递同一个回调任务。
void FoxThreadPool::broadcast(callback cb) {
    for(size_t i = 0; i < m_threads.size(); ++i) {
        m_threads[i]->dispatch(cb);
    }
}

// dump: 输出线程池整体状态与各子线程状态。
void FoxThreadPool::dump(std::ostream& os) {
    RWMutex::ReadLock lock(m_mutex);
    os << "[FoxThreadPool name = " << m_name << " thread_count = " << m_threads.size()
       << " tasks = " << m_callbacks.size() << " total = " << m_total
       << " advance = " << m_advance
       << "]" << std::endl;
    for(size_t i = 0; i < m_threads.size(); ++i) {
        os << "    ";
        m_threads[i]->dump(os);
    }
}

// GetThis: 获取当前线程局部的 FoxThread 指针。
FoxThread* FoxThread::GetThis() {
    return s_thread;
}

// GetFoxThreadName: 返回当前线程名称。若未注册则自动赋予 UNNAME_tID。
const std::string& FoxThread::GetFoxThreadName() {
    FoxThread* t = GetThis();
    if(t) {
        return t->m_name;
    }

    uint64_t tid = sherry::GetThreadId();
    do {
        RWMutex::ReadLock lock(s_thread_mutex);
        auto it = s_thread_names.find(tid);
        if(it != s_thread_names.end()) {
            return it->second;
        }
    } while(0);

    do {
        RWMutex::WriteLock lock(s_thread_mutex);
        s_thread_names[tid] = "UNNAME_" + std::to_string(tid);
        return s_thread_names[tid];
    } while (0);
}

// GetAllFoxThreadName: 获取所有已注册的线程 id->name 映射。
void FoxThread::GetAllFoxThreadName(std::map<uint64_t, std::string>& names) {
    RWMutex::ReadLock lock(s_thread_mutex);
    for(auto it = s_thread_names.begin();
            it != s_thread_names.end(); ++it) {
        names.insert(*it);
    }
}

// setThis: 在线程内调用, 设定线程局部指针与名称注册。
// 注意: 会将 m_name 追加 _threadId, 可能重复追加。
void FoxThread::setThis() {
    m_name = m_name + "_" + std::to_string(sherry::GetThreadId());
    s_thread = this;

    RWMutex::WriteLock lock(s_thread_mutex);
    s_thread_names[sherry::GetThreadId()] = m_name;
}

// unsetThis: 清理线程局部注册。
void FoxThread::unsetThis() {
    s_thread = nullptr;
    RWMutex::WriteLock lock(s_thread_mutex);
    s_thread_names.erase(sherry::GetThreadId());
}

// get: 根据名称查找线程或线程池实例。
IFoxThread::ptr FoxThreadManager::get(const std::string& name) {
    auto it = m_threads.find(name);
    return it == m_threads.end() ? nullptr : it->second;
}

// add: 添加一个名称->线程(池) 映射, 覆盖同名旧值。
void FoxThreadManager::add(const std::string& name, IFoxThread::ptr thr) {
    m_threads[name] = thr;
}

// dispatch: 向指定名称的线程或线程池分发单任务。
// 若 name 未找到会触发断言。
void FoxThreadManager::dispatch(const std::string& name, callback cb) {
    IFoxThread::ptr ti = get(name);
    SYLAR_ASSERT(ti);
    ti->dispatch(cb);
}

// dispatch(id): 向指定名称的线程池中某索引线程分发任务。
void FoxThreadManager::dispatch(const std::string& name, uint32_t id, callback cb) {
    IFoxThread::ptr ti = get(name);
    SYLAR_ASSERT(ti);
    ti->dispatch(id, cb);
}

// batchDispatch: 批量分发任务到指定线程/线程池。
void FoxThreadManager::batchDispatch(const std::string& name, const std::vector<callback>& cbs) {
    IFoxThread::ptr ti = get(name);
    SYLAR_ASSERT(ti);
    ti->batchDispatch(cbs);
}

// broadcast: 向指定名称的线程或池广播一个任务。
void FoxThreadManager::broadcast(const std::string& name, callback cb) {
    IFoxThread::ptr ti = get(name);
    SYLAR_ASSERT(ti);
    ti->broadcast(cb);
}

// dumpFoxThreadStatus: 输出所有受管线程/线程池状态与当前已注册名字列表。
void FoxThreadManager::dumpFoxThreadStatus(std::ostream& os) {
    os << "FoxThreadManager: " << std::endl;
    for(auto it = m_threads.begin();
            it != m_threads.end(); ++it) {
        it->second->dump(os);
    }

    os << "All FoxThreads:" << std::endl;
    std::map<uint64_t, std::string> names;
    FoxThread::GetAllFoxThreadName(names);
    for(auto it = names.begin();
            it != names.end(); ++it) {
        os << std::setw(30) << it->first
           << ": " << it->second << std::endl;
    }
}

// init: 根据配置构造线程或线程池。
// 配置项: num(线程数量), advance(是否启用高级调度)。
void FoxThreadManager::init() {
    auto m = g_thread_info_set->getValue();
    for(auto i : m) {
        auto num = sherry::GetParamValue(i.second, "num", 0);
        auto name = i.first;
        auto advance = sherry::GetParamValue(i.second, "advance", 0);
        if(num <= 0) {
            SYLAR_LOG_ERROR(g_logger) << "thread pool:" << name
                        << " num:" << num
                        << " advance:" << advance
                        << " invalid";
            continue;
        }
        if(num == 1) {
            m_threads[i.first] = FoxThread::ptr(new FoxThread(i.first));
            SYLAR_LOG_INFO(g_logger) << "init thread : " << i.first;
        } else {
            m_threads[i.first] = FoxThreadPool::ptr(new FoxThreadPool(
                            num, name, advance));
            SYLAR_LOG_INFO(g_logger) << "init thread pool:" << name
                       << " num:" << num
                       << " advance:" << advance;
        }
    }
}

// start: 启动所有线程/线程池。
void FoxThreadManager::start() {
    for(auto i : m_threads) {
        SYLAR_LOG_INFO(g_logger) << "thread: " << i.first << " start begin";
        i.second->start();
        SYLAR_LOG_INFO(g_logger) << "thread: " << i.first << " start end";
    }
}

// stop: 停止并 join 所有线程/线程池。
void FoxThreadManager::stop() {
    for(auto i : m_threads) {
        SYLAR_LOG_INFO(g_logger) << "thread: " << i.first << " stop begin";
        i.second->stop();
        SYLAR_LOG_INFO(g_logger) << "thread: " << i.first << " stop end";
    }
    for(auto i : m_threads) {
        SYLAR_LOG_INFO(g_logger) << "thread: " << i.first << " join begin";
        i.second->join();
        SYLAR_LOG_INFO(g_logger) << "thread: " << i.first << " join end";
    }
}

}