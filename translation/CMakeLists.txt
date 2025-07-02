cmake_minimum_required(VERSION 3.16)
project(translation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_library(translation STATIC
    translation.cpp
    translation.h
)

target_include_directories(translation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(translation
    PRIVATE
        Qt6::Core
        Qt6::Network
)
