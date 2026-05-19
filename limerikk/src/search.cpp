#include <array>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <unordered_set>

#include "limerikk.h"

Move Position::best_move(int depth, std::atomic<bool>& should_stop, Budgeter* budgeter, bool enable_uci_info, int64_t* score_out) {
    (void)depth;
    (void)should_stop;
    (void)budgeter;
    (void)enable_uci_info;
    (void)score_out;

    MoveList moves = generate_moves();
    filter_moves(moves);

    if (moves.count == 0) {
        return NULL_MOVE;
    }

    return moves.data[moves.count/2];
}

Move Position::think(int depth, std::atomic<bool>& should_stop, Budgeter* budgeter, bool enable_uci_info, int64_t* score_out) {
    uint64_t hash = encode_polyglot();
    std::vector<PolyglotEntry> p_moves = probe_book(hash);

    if (!p_moves.empty()) {
        PolyglotEntry line = choose_move(p_moves);
        Move move = decode_polyglot(line);
        assert(is_move_legal_slow(move));
        return move;
    } else {
        Move best = best_move(depth, should_stop, budgeter, enable_uci_info, score_out);
        assert(best != NULL_MOVE);
        return best;
    }
}