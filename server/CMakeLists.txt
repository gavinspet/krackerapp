cmake_minimum_required(VERSION 3.16)
project(krackerapp_server LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(Drogon REQUIRED)
find_package(PostgreSQL REQUIRED)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(krackerapp_server
  src/main.cpp
  src/utils/Jwt.cc
  src/utils/Crypto.cc      
)

target_include_directories(krackerapp_server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${REPO_ROOT}/extern/jwt-cpp/include
)

# Find argon2 (system lib)
find_library(ARGON2_LIB argon2 REQUIRED)

target_link_libraries(krackerapp_server PRIVATE
  Drogon::Drogon
  PostgreSQL::PostgreSQL
  ${ARGON2_LIB}             
)
