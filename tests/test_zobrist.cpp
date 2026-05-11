#include <catch2/catch_test_macros.hpp>
#include "limerikk.h"
#include <array>

void zobrist_search(int depth, Position& position) {
    uint64_t incremental_hash = position.zobrist;
    uint64_t new_hash = position.compute_zobrist();

    if (new_hash != incremental_hash) {
        position.display();
    }

    REQUIRE(new_hash == incremental_hash);

    int my_side = position.to_move;

    if (depth == 0) {
        return;
    } else {
        MoveList moves = position.generate_moves();

        for (Move move : moves) {
            position.make_move(move);
            position.verify_integrity();

            if (!position.is_checked[my_side]) {
                zobrist_search(depth-1, position);
            }
            
            position.unmake_move();
            position.verify_integrity();
        }

        if (!position.is_checked[my_side]) {
            position.make_null_move();

            if (!position.is_checked[my_side]) {
                zobrist_search(depth-1, position);
            }

            position.unmake_null_move();
        }
    }
}

TEST_CASE("Zobrist - incremental zobrist equals compute_zobrist | STARTING POSITION") {
    Position pos = *Position::parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    zobrist_search(5, pos);
}

TEST_CASE("Zobrist - incremental zobrist equals compute_zobrist | KIWIPETE POSITION") {
    Position pos = *Position::parse_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    zobrist_search(5, pos);
}