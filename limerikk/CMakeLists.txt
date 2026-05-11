find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(GENERATED_DIR ${CMAKE_SOURCE_DIR}/generated)

set(MOVE_TABLES_HPP ${GENERATED_DIR}/generated_tables.h)
set(NNUE_HPP ${GENERATED_DIR}/nnue_embed.h)
set(BOOK_HPP ${GENERATED_DIR}/opening_book.h)

set(BOOK_BIN ${CMAKE_CURRENT_LIST_DIR}/meta/Titans.bin)
set(MODEL_BIN ${CMAKE_CURRENT_LIST_DIR}/meta/model.bin)

add_executable(precompute_tables "meta/precompute_tables.cpp")
add_executable(bin_to_header "meta/bin_to_header.cpp")

set_target_properties(precompute_tables PROPERTIES
  MAP_IMPORTED_CONFIG_DEBUG Release
  MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
  MAP_IMPORTED_CONFIG_MINSIZEREL Release
)

add_custom_command(
    OUTPUT ${MOVE_TABLES_HPP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND precompute_tables ${MOVE_TABLES_HPP}
    DEPENDS precompute_tables
    COMMAND_EXPAND_LISTS
)

add_custom_command(
    OUTPUT ${BOOK_HPP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND bin_to_header ${BOOK_BIN} ${BOOK_HPP}
    DEPENDS bin_to_header ${BOOK_BIN}
    COMMAND_EXPAND_LISTS
)

add_custom_command(
    OUTPUT ${NNUE_HPP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/meta/nnue_embed.py ${MODEL_BIN} ${NNUE_HPP}
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/meta/nnue_embed.py ${MODEL_BIN}
    COMMAND_EXPAND_LISTS
)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)

add_library(limerikk STATIC ${SOURCES} ${MOVE_TABLES_HPP} ${BOOK_HPP} ${NNUE_HPP})
target_include_directories(limerikk PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src)
target_include_directories(limerikk PRIVATE ${GENERATED_DIR})