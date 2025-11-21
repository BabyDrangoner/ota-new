cmake_minimum_required(VERSION 2.8)
project(sherry)

include(./cmake/utils.cmake)

# 设置C++17标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -O0 -ggdb -std=c++17 -Wall -Wno-deprecated -Werror -Wno-unused-function -Wno-builtin-macro-redefined")

# ===============================
# 头文件路径
# ===============================
include_directories(
    .
    ${PROJECT_SOURCE_DIR}/include
    /build/yaml-cpp-0.8.0/include
)

# ===============================
# 库文件路径
# ===============================
link_directories(
    /apps/sherry/lib
)

# ===============================
# 查找依赖库
# ===============================
find_library(YAMLCPP NAMES yaml-cpp)
find_library(SSL_LIB NAMES ssl)
find_library(CRYPTO_LIB NAMES crypto)
find_library(PAHO_MQTTPP3 NAMES paho-mqttpp3)
find_library(PAHO_MQTT3AS NAMES paho-mqtt3as)
find_library(EVENT_LIB NAMES event)

# hiredis 与 hiredis_vip
find_library(HIREDIS_LIB NAMES hiredis PATHS /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu)
find_library(HIREDIS_VIP_LIB NAMES hiredis_vip PATHS /usr/local/lib /usr/lib/x86_64-linux-gnu)

if(NOT HIREDIS_LIB)
    message(FATAL_ERROR "hiredis not found! Please install: sudo apt install libhiredis-dev")
else()
    message(STATUS "Found hiredis: ${HIREDIS_LIB}")
endif()

if(NOT HIREDIS_VIP_LIB)
    message(WARNING "hiredis_vip not found. Redis Cluster functions may fail.")
else()
    message(STATUS "Found hiredis_vip: ${HIREDIS_VIP_LIB}")
endif()


# ===============================
# 源码文件列表
# ===============================
set(LIB_SRC
    sherry/address.cc
    sherry/bytearray.cc
    sherry/log.cc
    sherry/fd_manager.cc
    sherry/fiber.cc
    sherry/http/http.cc
    sherry/util.cc
    sherry/hook.cc
    sherry/scheduler.cc
    sherry/socket.cc
    sherry/timer.cc
    sherry/config.cc
    sherry/thread.cc
    sherry/iomanager.cc
    sherry/tcp_server.cc
    sherry/stream.cc
    sherry/socket_stream.cc
    sherry/ota_manager.cc
    sherry/ota_notifier.cc
    sherry/ota_subscribe_download.cc
    sherry/ota_query_responder.cc
    sherry/ota_client_callback.cc
    sherry/mqtt_client.cc

    sherry/http/http11_parser.rl.cc
    sherry/http/httpclient_parser.rl.cc
    sherry/http/http_parser.cc
    sherry/http/http_session.cc
    sherry/http/http_server.cc
    sherry/http/servlet.cc

    sherry/util/hash_util.cc
    sherry/db/fox_thread.cc
    sherry/db/redis.cc
    sherry/db/redis_util.cc
)

# ===============================
# 构建共享库
# ===============================
add_library(sherry SHARED ${LIB_SRC})
force_redefine_file_macro_for_sources(sherry)  # __FILE__

# ===============================
# 链接库总表
# ===============================
set(LIBS
    sherry
    dl
    pthread
    ${YAMLCPP}
    ${PAHO_MQTTPP3}
    ${PAHO_MQTT3AS}
    ${SSL_LIB}
    ${CRYPTO_LIB}
    ${EVENT_LIB}
    ${HIREDIS_LIB}
    ${HIREDIS_VIP_LIB}  
)

message(STATUS "*** Final link libraries: ${LIBS}")

# ===============================
# 可执行文件目标
# ===============================
macro(add_sherry_executable target)
    add_executable(${target} tests/${target}.cc)
    add_dependencies(${target} sherry)
    force_redefine_file_macro_for_sources(${target})
    target_link_libraries(${target} ${LIBS})
endmacro()

add_sherry_executable(test)
add_sherry_executable(test_config)
add_sherry_executable(test_thread)
add_sherry_executable(test_util)
add_sherry_executable(test_fiber)
add_sherry_executable(test_scheduler)
add_sherry_executable(test_iomanager)
add_sherry_executable(test_hook)
add_sherry_executable(test_address)
add_sherry_executable(test_socket)
add_sherry_executable(test_bytearray)
add_sherry_executable(test_server)
add_sherry_executable(test_http)
add_sherry_executable(test_http_parser)
add_sherry_executable(test_tcp_server)
add_sherry_executable(test_http_server)
add_sherry_executable(test_ota_server)
add_sherry_executable(test_redis)
add_sherry_executable(test_hash)
add_sherry_executable(test_mqtt_client)


add_executable(echo_server examples/echo_server.cc)
add_dependencies(echo_server sherry)
force_redefine_file_macro_for_sources(echo_server)
target_link_libraries(echo_server ${LIBS})

# ===============================
# 输出路径
# ===============================
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
