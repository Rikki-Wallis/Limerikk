
include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2   # Use a specific version
  SYSTEM
)

FetchContent_MakeAvailable(Catch2)

add_executable(test_perft test_perft.cpp)
target_link_libraries(test_perft PRIVATE limerikk Catch2::Catch2WithMain)
add_executable(test_misc test_misc.cpp)
target_link_libraries(test_misc PRIVATE limerikk Catch2::Catch2WithMain)
add_executable(test_zobrist test_zobrist.cpp)
target_link_libraries(test_zobrist PRIVATE limerikk Catch2::Catch2WithMain)
add_executable(test_eval test_eval.cpp)
target_link_libraries(test_eval PRIVATE limerikk Catch2::Catch2WithMain)
add_executable(test_polyglot test_polyglot.cpp)
target_link_libraries(test_polyglot PRIVATE limerikk Catch2::Catch2WithMain)
add_executable(test_fen test_fen.cpp)
target_link_libraries(test_fen PRIVATE limerikk Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(test_perft)
catch_discover_tests(test_misc)
catch_discover_tests(test_zobrist)
catch_discover_tests(test_eval)
catch_discover_tests(test_polyglot)
catch_discover_tests(test_fen)
include(FetchContent)
