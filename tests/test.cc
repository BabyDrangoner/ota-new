// tests/test.cc
#include <iostream>
#include "../sherry/log.h"
#include "../sherry/util.h"
int main() {
    sherry::Logger::ptr logger(new sherry::Logger);
    // logger->addAppender(sherry::LogAppender::ptr(new sherry::StdoutLogAppender));

    sherry::FileLogAppender::ptr file_appender(new sherry::FileLogAppender("./log.txt"));
    
    sherry::LogFormatter::ptr fmt(new sherry::LogFormatter("%d%T%p%T%m%n"));

    file_appender->setFormatter(fmt);
    file_appender->setLevel(sherry::LogLevel::DEBUG);
    logger->addAppender(file_appender);



    //sherry::LogEvent::ptr event (new sherry::LogEvent(__FILE__, __LINE__, 0, sherry::GetThreadId(), sherry::GetFiberId(), time(0)));
    //event->getSS() << "hello sherry log";

    //logger->log(sherry::LogLevel::DEBUG, event);
    //std::cout << "hello sherry log" << std::endl;

    SYLAR_LOG_INFO(logger) << "test macro";
    SYLAR_LOG_ERROR(logger) << "test macro error";

    SYLAR_LOG_FMT_ERROR(logger, "test macro fmt error %s", "aa");

    auto l = sherry::LoggerMgr::GetInstance()->getLogger("xx");
    SYLAR_LOG_INFO(l) << "xxx";
    return 0;
}
