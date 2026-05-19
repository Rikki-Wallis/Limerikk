cmake_minimum_required(VERSION 3.16)
project(Limerikk LANGUAGES CXX)

include(CTest)
enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(
        /W4 /WX /arch:AVX2 /wd4324 /wd4505
    )
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic -Werror -Wimplicit-int-conversion -Wno-unused-const-variable -march=native -Wno-c2y-extensions -Wno-unused-function
    )
endif()

add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

add_subdirectory(limerikk)
add_subdirectory(uci)
add_subdirectory(tests)
add_subdirectory(datagen)
add_subdirectory(eval)
