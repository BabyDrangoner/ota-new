#ifndef _SHERRY_HTTP_UTIL_H__
#define _SHERRY_HTTP_UTIL_H__

#include "http.h"
namespace sherry{
namespace http{

void setServerError(HttpResponse::ptr rsp){
    rsp->setStatus(HttpStatus::OK);
    rsp->setBody("server error!");
}

}
}



#endif