cmake_minimum_required(VERSION 3.7)
project(dpi)

set(CMAKE_CXX_STANDARD 14)
set(Boost_USE_MULTITHREADED ON)

find_package(Boost REQUIRED COMPONENTS program_options system)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include(FindPkgConfig)

pkg_search_module(SDL2 sdl2)

include_directories(${SDL2_INCLUDE_DIRS} ${SDL2IMAGE_INCLUDE_DIRS})

add_library(dpi_common
		common.cpp
		common.h
		dpi.h)
target_link_libraries(dpi_common netvid)

set(FB_RENDER_SOURCE_FILES fb_render.cpp)

set(FB_RENDER_TEST_SOURCE_FILES fb_render_test.cpp)

set(SDL_RENDER_SOURCE_FILES sdl_render.cpp)

add_executable(fb_render ${FB_RENDER_SOURCE_FILES})
target_link_libraries(fb_render ${Boost_LIBRARIES} Threads::Threads dpi_common netvid)

add_executable(fb_render_test ${FB_RENDER_TEST_SOURCE_FILES})
target_link_libraries(fb_render_test ${Boost_LIBRARIES} Threads::Threads dpi_common netvid)

if (SDL2_FOUND)
	add_executable(sdl_render ${SDL_RENDER_SOURCE_FILES})
	target_link_libraries(sdl_render ${Boost_LIBRARIES} Threads::Threads ${SDL2_LIBRARIES} dpi_common netvid)
endif ()
