#include <array>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <unordered_set>

#include "limerikk.h"

struct SearchContext {
    int node_count;
    Budgeter* budgeter;
    std::atomic<bool>& should_stop;
    bool exited;

    bool exit_on_node() {
        if (should_stop) {
            exited = true;
        }

        node_count++;

        if ((node_count & 0xfff) == 0) {
            if (budgeter->should_exit(node_count)) {
                exited = true;
            }
        }

        return exited;
    }
};

static int32_t search(Position& pos, SearchContext& s, int depth, int ply) {
    if (s.exit_on_node()) {
        return 0;
    }

    MoveList moves = pos.generate_moves();
    pos.filter_moves(moves);

    if (moves.count == 0) {
        if (pos.is_checked[pos.to_move]) {
            return -MATE_SCORE + ply;
        }
        else {
            return 0;
        }
    }

    if (depth == 0) {
        return pos.signed_eval();
    }

    int32_t best_score = -INF_SCORE;

    for (Move mv : moves) {
        pos.make_move(mv);

        int32_t score = -search(pos, s, depth-1, ply+1);

        if (score > best_score) {
            best_score = score;
        }

        pos.unmake_move();
    }

    return best_score;
}

static std::pair<Move, int32_t> best_move(Position& pos, SearchContext& s, int depth) {
    assert(depth > 0);

    if (s.exit_on_node()) {
        return {NULL_MOVE, -INF_SCORE};
    }

    MoveList moves = pos.generate_moves();
    pos.filter_moves(moves);

    int32_t best_score = -INF_SCORE;
    Move best_move = NULL_MOVE;

    for (Move mv : moves) {
        pos.make_move(mv);

        int32_t score = -search(pos, s, depth-1, 1);

        if (score > best_score) {
            best_score = score;
            best_move = mv;
        }

        pos.unmake_move();
    }

    return {
        best_move,
        best_score
    };
}

Move Position::best_move(int depth, std::atomic<bool>& should_stop, Budgeter* budgeter, bool enable_uci_info, int64_t* score_out) {
    (void)enable_uci_info;
    (void)score_out;

    budgeter->init();

    TimePoint start = Clock::now();

    SearchContext s = {
        .node_count = 0,
        .budgeter = budgeter,
        .should_stop = should_stop,
    };

    Move best_move = NULL_MOVE;
    
    for (int d = 1; d < depth; ++d) {
        auto [mv, score] = ::best_move(*this, s, d);

        if (s.exited) {
            break;
        }

        if (enable_uci_info) {
            std::string score_info;

            if (std::abs(score) > (MATE_SCORE - 1000)) {
                int mate_in = MATE_SCORE - std::abs(score);
                score_info = std::format("mate {}{}", score < 0 ? "-" : "", mate_in);
            }
            else {
                score_info = std::format("cp {}", score);
            }

            long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
            auto seconds = double(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count()) / 1000000;

            int nps = int(float(s.node_count) / float(seconds));

            print("info depth {} time {} nodes {} nps {} score {} pv {}\n", d, ms, s.node_count, nps, score_info, to_uci_move(mv));
        }

        best_move = mv;
    }

    return best_move;
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