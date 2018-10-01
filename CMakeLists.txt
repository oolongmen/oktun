cmake_minimum_required(VERSION 2.8)

project(oktun)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "-Wall -g")

include_directories(./src/)
include_directories(./thirdparties)

set(oktun_server_src
    ./src/oktun.h
    ./src/oktun_utils.h
    ./src/oktun_buffer.h
    ./src/oktun_buffer.cpp
    ./thirdparties/kcp/ikcp.h
    ./thirdparties/kcp/ikcp.c
    ./src/oktun_server.h
    ./src/oktun_server.cpp
    ./src/server.cpp)

add_executable(oktun_server ${oktun_server_src})

target_link_libraries(oktun_server -static-libgcc -static-libstdc++ event pthread)

set(oktun_client_src
    ./src/oktun.h
    ./src/oktun_itunnel.h
    ./src/oktun_utils.h
    ./src/oktun_buffer.h
    ./src/oktun_buffer.cpp
    ./thirdparties/kcp/ikcp.h
    ./thirdparties/kcp/ikcp.c
    ./src/oktun_client.h
    ./src/oktun_client.cpp
    ./src/oktun_proxy.h
    ./src/oktun_proxy.cpp
    ./src/client.cpp)

add_executable(oktun_client ${oktun_client_src})

target_link_libraries(oktun_client -static-libgcc -static-libstdc++ event pthread)

