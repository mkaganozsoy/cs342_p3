cmake_minimum_required(VERSION 3.27)
project(Project3 C)

set(CMAKE_C_STANDARD 11)

add_executable(Project3 mfserver.c
        mf.c
        mf.h
        test.c
        app2.c
        app1.c)
