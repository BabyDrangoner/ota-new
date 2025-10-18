#ifndef __SYLAR_UTIL_H__
#define __SYLAR_UTIL_H__

#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <boost/lexical_cast.hpp>


namespace sherry{

pid_t GetThreadId();

uint32_t GetFiberId();

void Backtrace(std::vector<std::string> & bt, int size, int skip = 1);
std::string BacktraceToString(int size = 64, int skip = 2, const std::string & prefix = "");

// 时间ms
uint64_t GetCurrentMS();
uint64_t GetCurrentUS();

std::stringstream FormatOtaPrex(uint16_t device_type, uint32_t device_no=0);

class TypeUtil {
public:
    static int8_t ToChar(const std::string& str);
    static int64_t Atoi(const std::string& str);
    static double Atof(const std::string& str);
    static int8_t ToChar(const char* str);
    static int64_t Atoi(const char* str);
    static double Atof(const char* str);
};

class Atomic {
public:
    template<class T, class S = T>
    static T addFetch(volatile T& t, S v = 1) {
        return __sync_add_and_fetch(&t, (T)v);
    }

    template<class T, class S = T>
    static T subFetch(volatile T& t, S v = 1) {
        return __sync_sub_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T orFetch(volatile T& t, S v) {
        return __sync_or_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T andFetch(volatile T& t, S v) {
        return __sync_and_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T xorFetch(volatile T& t, S v) {
        return __sync_xor_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T nandFetch(volatile T& t, S v) {
        return __sync_nand_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T fetchAdd(volatile T& t, S v = 1) {
        return __sync_fetch_and_add(&t, (T)v);
    }

    template<class T, class S>
    static T fetchSub(volatile T& t, S v = 1) {
        return __sync_fetch_and_sub(&t, (T)v);
    }

    template<class T, class S>
    static T fetchOr(volatile T& t, S v) {
        return __sync_fetch_and_or(&t, (T)v);
    }

    template<class T, class S>
    static T fetchAnd(volatile T& t, S v) {
        return __sync_fetch_and_and(&t, (T)v);
    }

    template<class T, class S>
    static T fetchXor(volatile T& t, S v) {
        return __sync_fetch_and_xor(&t, (T)v);
    }

    template<class T, class S>
    static T fetchNand(volatile T& t, S v) {
        return __sync_fetch_and_nand(&t, (T)v);
    }

    template<class T, class S>
    static T compareAndSwap(volatile T& t, S old_val, S new_val) {
        return __sync_val_compare_and_swap(&t, (T)old_val, (T)new_val);
    }

    template<class T, class S>
    static bool compareAndSwapBool(volatile T& t, S old_val, S new_val) {
        return __sync_bool_compare_and_swap(&t, (T)old_val, (T)new_val);
    }
};

template<class V, class Map, class K>
V GetParamValue(const Map& m, const K& k, const V& def = V()) {
    auto it = m.find(k);
    if(it == m.end()) {
        return def;
    }
    try {
        return boost::lexical_cast<V>(it->second);
    } catch (...) {
    }
    return def;
}

template<class V, class Map, class K>
bool CheckGetParamValue(const Map& m, const K& k, V& v) {
    auto it = m.find(k);
    if(it == m.end()) {
        return false;
    }
    try {
        v = boost::lexical_cast<V>(it->second);
        return true;
    } catch (...) {
    }
    return false;
}

}

#endif
