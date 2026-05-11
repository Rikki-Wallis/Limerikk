#include <catch2/catch_test_macros.hpp>

#include <unordered_set>

#include "limerikk.h"

static void test_capture_gen(Position& pos, int depth) {
    MoveList moves = pos.generate_moves();
    MoveList captures = pos.generate_captures();

    pos.filter_moves(moves);
    pos.filter_moves(captures);

    // recurse

    if (depth > 0) {
        for (Move mv : moves) {
            pos.make_move(mv);
            test_capture_gen(pos, depth-1);
            pos.unmake_move();
        }
    }

    // remove non-captures

    for (int i = moves.count-1; i >= 0; --i) {
        Move mv = moves.data[i];

        if (!is_capture(mv)) {
            moves.data[i] = moves.data[--moves.count];
        }
    }

    // compare

    std::unordered_set<Move> move_set;
    std::unordered_set<Move> capture_set;

    for (Move mv : moves) {
        move_set.insert(mv);
    }

    for (Move mv : captures) {
        capture_set.insert(mv);
    }

    REQUIRE(move_set == capture_set);
}

TEST_CASE("Generate-Captures Equals Captures from Generate-Moves") {
    auto pos = *Position::parse_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    test_capture_gen(pos, 4); 
}

static void test_pin_case(const std::string& fen, const std::unordered_set<int>& white_pins, const std::unordered_set<int>& black_pins) {
    auto pos = *Position::parse_fen(fen);

    uint64_t white_mask = pos.generate_pin_mask(WHITE);
    uint64_t black_mask = pos.generate_pin_mask(BLACK);

    std::unordered_set<int> white_set{};
    std::unordered_set<int> black_set{};

    for (int x : set_bits(white_mask)) {
        white_set.insert(x);
    }

    for (int x : set_bits(black_mask)) {
        black_set.insert(x);
    }

    REQUIRE(white_set == white_pins);
    REQUIRE(black_set == black_pins);
}

TEST_CASE("Pin-mask is correct") {
    test_pin_case("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1 ", {33}, {29});
    test_pin_case("k1b4R/8/5r2/8/5P2/8/5K2/8 w - - 0 1", {29}, {58});
    test_pin_case("k1b4R/8/5r2/5b2/5P2/8/5K2/8 w - - 0 1", {}, {58});
    test_pin_case("k1b3bR/8/5r2/5b2/5P2/8/5K2/8 w - - 0 1", {}, {});
    test_pin_case("k1b3bR/8/5r2/5b2/5P2/5r2/5K2/8 w - - 0 1", {}, {});
    test_pin_case("k1b3bR/8/1q3r2/5b2/3B1P2/5r2/5K2/8 w - - 0 1", {27}, {});
    test_pin_case("k1b3bR/8/1q3r2/5b2/3B1P2/4Pr2/5K2/8 w - - 0 1", {}, {});
    test_pin_case("k1b3bR/1p6/1q3r2/5b2/3BBP2/4Pr2/5K2/8 w - - 0 1", {}, {49});
    test_pin_case("k1b3bR/1p6/1q3r2/r4b2/3BBP2/4Pr2/5K2/R7 w - - 0 1", {}, {49, 32});
}
