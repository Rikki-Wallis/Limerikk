#include <catch2/catch_test_macros.hpp>

#include "limerikk.h"

static void test_fen(Position& pos, int depth) {
    std::string fen = pos.fen();
    auto maybe_pos = Position::parse_fen(fen);

    REQUIRE(maybe_pos.has_value());
    auto pos2 = std::move(*maybe_pos);

    REQUIRE(pos2.zobrist == pos.zobrist);

    if (depth == 0) {
        return;
    }

    MoveList moves = pos.generate_moves();

    int my_side = pos.to_move;

    for (Move mv : moves) {
        pos.make_move(mv);

        if (!pos.is_checked[my_side]) {
            test_fen(pos, depth-1);
        }

        pos.unmake_move();
    }
}

TEST_CASE("FEN encode and decode is correct") {
    Position pos = *Position::parse_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    test_fen(pos, 3);
}