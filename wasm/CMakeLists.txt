cmake_minimum_required(VERSION 3.16)
project(BlunderfishWASM LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
    -Wall -Wextra
    -Wno-unused-const-variable
    -Wno-unused-function
    -Wno-unused-parameter
    -O2
)

add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

set(GENERATED_DIR    ${CMAKE_SOURCE_DIR}/../generated)
set(BLUNDERFISH_SRC  ${CMAKE_SOURCE_DIR}/../blunderfish/src)

file(GLOB_RECURSE BLUNDERFISH_SOURCES CONFIGURE_DEPENDS ${BLUNDERFISH_SRC}/*.cpp)

add_library(blunderfish STATIC ${BLUNDERFISH_SOURCES})
target_include_directories(blunderfish PUBLIC  ${BLUNDERFISH_SRC})
target_include_directories(blunderfish PRIVATE ${GENERATED_DIR})

add_executable(blunderfish_wasm wasm_main.cpp)
target_link_libraries(blunderfish_wasm PRIVATE blunderfish)

target_link_options(blunderfish_wasm PRIVATE
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=67108864
    -sSTACK_SIZE=8388608
    -sMODULARIZE=1
    -sEXPORT_NAME=BlunderfishUCI
    -sEXPORTED_FUNCTIONS=_main,_uci_init,_uci_send,_uci_flush
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap
    -sEXIT_RUNTIME=0
    -O2
)
