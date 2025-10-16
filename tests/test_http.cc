#include "../sherry/http/http.h"
#include "../sherry/log.h"

void test_request(){
    sherry::http::HttpRequest::ptr req(new sherry::http::HttpRequest);
    req->setHeader("host", "sherry.top");
    req->setBody("hello sherry");

    req->dump(std::cout) << std::endl;
}

void test_response(){
    sherry::http::HttpResponse::ptr rsp(new sherry::http::HttpResponse);
    rsp->setHeader("X-X", "sherry");
    rsp->setBody("hello sherry");
    rsp->setClose(false);
    rsp->setStatus((sherry::http::HttpStatus)400);
    rsp->dump(std::cout) << std::endl;
}

int main(){
    test_request();
    test_response();
    return 0;
}
