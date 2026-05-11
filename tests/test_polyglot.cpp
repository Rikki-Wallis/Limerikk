#include <catch2/catch_test_macros.hpp>
#include <string>
#include "limerikk.h"

TEST_CASE("Polyglot - Starting Position") {

    std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    Position pos = *Position::parse_fen(fen);
    uint64_t hash = pos.encode_polyglot();

    REQUIRE(hash == 0x463b96181691fc9c);
}

TEST_CASE("Polyglot - Starting Position (e2e4)") {
    std::string fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";

    Position pos = *Position::parse_fen(fen);
    uint64_t hash = pos.encode_polyglot();

    REQUIRE(hash == 0x0823c9b50fd114196);
}

TEST_CASE("Polyglot - Starting Position (e2e4 d7d5)") {
    std::string fen = "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2";
    
    Position pos = *Position::parse_fen(fen);
    uint64_t hash = pos.encode_polyglot();

    REQUIRE(hash == 0x0756b94461c50fb0);
}

TEST_CASE("Polyglot - Starting Position (e2e4 d7d5 e4e5)") {
    std::string fen = "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2";
    
    Position pos = *Position::parse_fen(fen);
    uint64_t hash = pos.encode_polyglot();

    REQUIRE(hash == 0x662fafb965db29d4);
}

TEST_CASE("Polyglot - Starting Position (e2e4 d7d5 e4e5 f7f5)") {
    std::string fen = "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3";
    
    Position pos = *Position::parse_fen(fen);
    uint64_t hash = pos.encode_polyglot();

    REQUIRE(hash == 0x22a48b5a8e47ff78);
}

TEST_CASE("Polyglot - Starting Position (e2e4 d7d5 e4e5 f7f5 e1e2)") {
    std::string fen = "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR b kq - 0 3";
    
    Position pos = *Position::parse_fen(fen);
    uint64_t hash = pos.encode_polyglot();

    REQUIRE(hash == 0x652a607ca3f242c1);
}

TEST_CASE("Polyglot - Starting Position (e2e4 d7d5 e4e5 f7f5 e1e2 e8f7)") {
    std::string fen = "rnbq1bnr/ppp1pkpp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR w - - 0 4";
    
    Position pos = *Position::parse_fen(fen);
    uint64_t hash = pos.encode_polyglot();

    REQUIRE(hash == 0x00fdd303c946bdd9);
}
TEST_CASE("Polyglot - Starting Position (a2a4 b7b5 h2h4 b5b4 c2c4)") {
    std::string fen = "rnbqkbnr/p1pppppp/8/8/PpP4P/8/1P1PPPP1/RNBQKBNR b KQkq c3 0 3";
    
    Position pos = *Position::parse_fen(fen);
    uint64_t hash = pos.encode_polyglot();

    REQUIRE(hash == 0x3c8123ea7b067637);
}
TEST_CASE("Polyglot - Starting Position (a2a4 b7b5 h2h4 b5b4 c2c4 b4c3 a1a3)") {
    std::string fen = "rnbqkbnr/p1pppppp/8/8/P6P/R1p5/1P1PPPP1/1NBQKBNR b Kkq - 0 4";
    
    Position pos = *Position::parse_fen(fen);
    uint64_t hash = pos.encode_polyglot();

    REQUIRE(hash == 0x5c3f9b829b279560);
}
