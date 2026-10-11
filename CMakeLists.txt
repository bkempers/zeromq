cmake_minimum_required(VERSION 3.24)
project(zmq_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(cppzmq REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

add_library(proto STATIC message.proto)
protobuf_generate(TARGET proto)
target_include_directories(proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(proto PUBLIC protobuf::libprotobuf)

add_executable(zmq_server server.cpp)
target_include_directories(zmq_server PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(zmq_server PRIVATE cppzmq proto)

add_executable(zmq_client client.cpp)
target_include_directories(zmq_client PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(zmq_client PRIVATE cppzmq proto)
