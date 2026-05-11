#include <catch2/catch_test_macros.hpp>
#include "limerikk.h"

static void check_eval(Position& position) {
    int64_t new_eval = position.compute_eval();

    REQUIRE(std::abs(new_eval - position.incr_eval) <= 2);
}

void eval_search(int depth, Position& position) {
    check_eval(position);
    int my_side = position.to_move;

    if (depth == 0) {
        return;
    } else {
        MoveList moves = position.generate_moves();

        for (Move move : moves) {
            position.make_move(move);
            position.verify_integrity();

            if (!position.is_checked[my_side]) {
                eval_search(depth-1, position);
            }
            
            position.unmake_move();
            position.verify_integrity();
        }

        if (!position.is_checked[my_side]) {
            position.make_null_move();

            if (!position.is_checked[my_side]) {
                eval_search(depth-1, position);
            }

            position.unmake_null_move();
        }
    }

    check_eval(position);
}

TEST_CASE("Eval - increment_eval equals eval | STARTING POSITION") {
    Position pos = *Position::parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    eval_search(3, pos);
}

TEST_CASE("Eval - increment_eval equals eval | KIWIPETE_POSITION") {
    Position pos = *Position::parse_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    eval_search(3, pos);
}