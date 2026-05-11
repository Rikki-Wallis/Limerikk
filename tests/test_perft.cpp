#include <catch2/catch_test_macros.hpp>

#include "limerikk.h"

TEST_CASE("Perft - Starting Position") {
    Position pos = *Position::parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    REQUIRE(perft_search(1, pos) == 20);
    REQUIRE(perft_search(2, pos) == 400);
    REQUIRE(perft_search(3, pos) == 8902);
    REQUIRE(perft_search(4, pos) == 197281);
    REQUIRE(perft_search(5, pos) == 4865609);
    REQUIRE(perft_search(6, pos) == 119060324);
}

TEST_CASE("Perft - Kiwipete Position") {    
    Position pos = *Position::parse_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");

    REQUIRE(perft_search(1, pos) == 48);
    REQUIRE(perft_search(2, pos) == 2039);
    REQUIRE(perft_search(3, pos) == 97862);
    REQUIRE(perft_search(4, pos) == 4085603);
    REQUIRE(perft_search(5, pos) == 193690690);
    //REQUIRE(perft_search(6, pos) == 8031647685);
}