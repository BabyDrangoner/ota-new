#include "../sherry/http/http_server.h"
#include "../sherry/log.h"

static sherry::Logger::ptr g_logger = SYLAR_LOG_ROOT();
sherry::IOManager::ptr worker;
void run(){
    sherry::http::HttpServer::ptr server(new sherry::http::HttpServer(true, worker.get(), sherry::IOManager::GetThis()));
    sherry::Address::ptr addr = sherry::Address::LookupAnyIPAddress("0.0.0.0:8020");

    while(!server->bind(addr)){
        sleep(2);
    }

    auto sd = server->getServletDispatch();
    sd->addServlet("/sherry/xx", [](sherry::http::HttpRequest::ptr req
                                ,sherry::http::HttpResponse::ptr rsp
                                ,sherry::http::HttpSession::ptr session){
        rsp->setBody(req->toString());

        return 0;
    });

    sd->addGlobServlet("/sherry/*", [](sherry::http::HttpRequest::ptr req
                                ,sherry::http::HttpResponse::ptr rsp
                                ,sherry::http::HttpSession::ptr session){
        rsp->setBody("Glob:\r\n" + req->toString());
        return 0;
    });
    server->start();
}

int main(int argc, char** argv){
    worker.reset(new sherry::IOManager(3, false, "worker"));
    sherry::IOManager iom(1, true, "main");
    iom.schedule(run);
    return 0;
}