#ifndef _SHERRY_HTTPSERVER_H__
#define _SHERRY_HTTPSERVER_H__

#include "../tcp_server.h"
#include "http_session.h"
#include "../iomanager.h"
#include "servlet.h"

namespace sherry{
namespace http{

class HttpServer : public TcpServer{
public:
    typedef std::shared_ptr<HttpServer> ptr;
    HttpServer(bool keepalive = false
               ,IOManager* worker = IOManager::GetThis()
               ,IOManager* accept_worker = IOManager::GetThis());

    ServletDispatch::ptr getServletDispatch() const { return m_dispatch;}
    void setServletDispatch(ServletDispatch::ptr v) { m_dispatch = v;}
protected:
    virtual void handleClient(Socket::ptr client) override;
private:
    bool m_isKeepalive;
    ServletDispatch::ptr m_dispatch;

};


}

}

#endif