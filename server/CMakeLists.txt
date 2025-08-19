cmake_minimum_required(VERSION 3.16)
project(krackerapp_server LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(Drogon REQUIRED)

add_executable(krackerapp_server src/main.cpp)
target_link_libraries(krackerapp_server PRIVATE Drogon::Drogon)
