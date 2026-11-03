cmake_minimum_required(VERSION 3.10)
project(romulus_router CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")

add_executable(romulus_router main.cpp)

# Thread-per-connection server needs the threading library.
find_package(Threads REQUIRED)
target_link_libraries(romulus_router PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(romulus_router PRIVATE ws2_32)
endif()
