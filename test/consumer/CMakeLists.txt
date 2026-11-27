cmake_minimum_required(VERSION 3.20)
project(ctar_consumer LANGUAGES CXX)

# Exercises the installed package exactly as a downstream user would.
find_package(ctar CONFIG REQUIRED)

add_executable(ctar_consumer main.cpp)
target_link_libraries(ctar_consumer PRIVATE ctar::ctar)
