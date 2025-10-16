#include "stream.h"

namespace sherry{
    int Stream::readFixSize(void* buffer, size_t length){
        size_t offset = 0;
        size_t left = length;
        while(left > 0){
            size_t len = read((char*)buffer + offset, left);
            if(len <= 0){
                return len; 
            }
            offset += len;
            left -= len;
        }
        return length;
    }

    int Stream::readFixSize(ByteArray::ptr ba, size_t length){
        size_t left =  length;
        while(left > 0){
            size_t len = read(ba, length);
            if(len <= 0){
                return len;
            }
            left -= len;
        }
        return length;
    }

    int Stream::writeFixSize(const void* buffer, size_t length){
        size_t offset = 0;
        size_t left = length;
        while(left > 0){
            size_t len = write((const char*)buffer + offset, left);
            if(len <= 0){
                return len;
            }
            offset += len;
            left -= len;
        }
        return length;
    }

    int Stream::writeFixSize(ByteArray::ptr ba, size_t length){
        size_t left = length;
        while(left > 0){
            size_t len = write(ba, left);
            if(len <= 0){
                return len;
            }
            left -= len;
        }
        return length;
    }

}