#include <cstdio>
#include <array>
#include "limerikk.h"

uint64_t perft_search(int depth, Position& position) {
    
    // Bulk count instead
    if (depth == 1) {
        MoveList moves = position.generate_moves();
        position.filter_moves(moves);
        return moves.count;
    }

    uint64_t nodes = 0;
    MoveList moves = position.generate_moves();

    int side = position.to_move;

    for (Move move : moves) {
        position.make_move(move);
        position.verify_integrity();

        if (!position.is_checked[side]) {
            nodes += perft_search(depth-1, position);
        }

        position.unmake_move();
        position.verify_integrity();
    }

    return nodes;
}