#include "socket_stream.h"
#include "log.h"

namespace sherry{

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

SocketStream::SocketStream(Socket::ptr sock, bool owner)
    :m_socket(sock)
    ,m_owner(owner){
    
}

SocketStream::~SocketStream(){
    if(m_owner && m_socket){
        m_socket->close();
    }
}

int SocketStream::read(void* buffer, size_t length){
    if(!isConnected()){
        SYLAR_LOG_DEBUG(g_logger) << "socket is disConnected";
        return -1;
    }
    return m_socket->recv(buffer, length);
}

int SocketStream::read(ByteArray::ptr ba, size_t length){
    if(!isConnected()){
        return -1;
    }
    std::vector<iovec> iovs;
    ba->getWriteBuffers(iovs, length);
    int rt = m_socket->recv(&iovs[0], iovs.size());
    if(rt > 0){
        ba->setPosition(ba->getPosition() + rt);
    }
    return rt;
}

int SocketStream::write(const void* buffer, size_t length){
    if(!isConnected()){
        return -1;
    }
    return m_socket->send(buffer, length);
}

int SocketStream::write(ByteArray::ptr ba, size_t length){
    if(!isConnected()){
        return -1;
    }

    std::vector<iovec> iovs;
    ba->getReadBuffers(iovs, length);
    int rt = m_socket->send(&iovs[0], iovs.size());
    if(rt > 0){
        ba->setPosition(ba->getPosition() + rt);
    }

    return rt;
}

int SocketStream::sendfile(int fd, off_t* offset, size_t length){
    if(!isConnected()){
        return -1;
    }

    return m_socket->sendFile(fd, offset, length);
}

void SocketStream::close(){
    if(m_socket){
        m_socket->close();
    }
}

bool SocketStream::isConnected() const{
    if(m_socket){
        return m_socket->isConnected();
    }

    return false;
}


}