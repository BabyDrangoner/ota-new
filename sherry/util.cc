#include "util.h"
#include <execinfo.h>
#include <sys/time.h>

#include "log.h"
#include "fiber.h"

sherry::Logger::ptr g_looger = SYLAR_LOG_NAME("system");

namespace sherry{

pid_t GetThreadId(){
    return syscall(SYS_gettid);
}

uint32_t GetFiberId(){
    return sherry::Fiber::GetFiberId();
}

void Backtrace(std::vector<std::string> & bt, int size, int skip){
    void ** array = (void **)malloc((sizeof(void *) * size));
    size_t s = ::backtrace(array, size);

    char ** strings = backtrace_symbols(array, s);
    if(strings == NULL){
        SYLAR_LOG_ERROR(g_looger) << "backtrace_synbols error";
        free(strings);
        free(array);
        return;
    }

    for(size_t i = skip;i < s;++i){
        bt.push_back(strings[i]);
    }
    free(strings);
    free(array);
}

std::string BacktraceToString(int size, int skip, const std::string & prefix){
    std::vector<std::string> bt;
    Backtrace(bt, size, skip);
    std::stringstream ss;
    for(size_t i = 0;i < bt.size();++i){
        ss << prefix << bt[i] << std::endl;
    }

    return ss.str();
}

uint64_t GetCurrentMS(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000ul + tv.tv_usec / 1000;
}
uint64_t GetCurrentUS(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 * 1000ul + tv.tv_usec;
}

std::stringstream FormatOtaPrex(uint16_t device_type, uint32_t device_no){
    std::stringstream ss;
    ss << "/ota/" << device_type;
    if(device_no != 0) ss << "/" << device_no;  
    return ss;
}

}