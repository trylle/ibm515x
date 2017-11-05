cmake_minimum_required(VERSION 3.7)
project(downsample)

set(CMAKE_CXX_STANDARD 14)
set(Boost_USE_MULTITHREADED ON)

find_package(Boost REQUIRED COMPONENTS program_options system)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffast-math")

add_library(downsample
        bayer.cpp
        bayer.h
        cga_downsample.cpp
        cga_downsample.h
        hsp.h
        parallel_process.cpp
        parallel_process.h)
target_link_libraries(downsample netvid)

add_executable(main main.cpp)
target_link_libraries(main ${Boost_LIBRARIES} Threads::Threads ${SDL2_LIBRARIES} netvid downsample)

add_executable(downsample_test test.cpp)
target_link_libraries(downsample_test ${Boost_LIBRARIES} Threads::Threads ${SDL2_LIBRARIES} netvid downsample)
