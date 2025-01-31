cmake_minimum_required(VERSION 3.30)
project(lock_free_vector)

set(CMAKE_CXX_STANDARD 20)

add_executable(lock_free_vector main.cpp
        lock-free-vector.cpp)
