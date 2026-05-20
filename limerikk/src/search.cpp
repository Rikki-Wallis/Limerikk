#include <array>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <unordered_set>

#include "limerikk.h"

struct SearchContext {
    int node_count;
    int qnode_count;

    int beta_cutoff_index_sum;
    int beta_cutoff_count;

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

struct MoveScores {
    int32_t data[256];
};

static int32_t see(const Position& pos, int sq, int side, Piece cur_piece, uint64_t occupancy) {
    int32_t value = 0;

    int attck_sq = pos.lowest_value_attacker(sq, side, occupancy);

    if (attck_sq != NULL_SQUARE) {
        Piece piece = Piece(pos.piece_at[attck_sq]);

        if (piece == PIECE_KING) {
            int x = pos.lowest_value_attacker(sq, opponent(side), occupancy ^ sq_to_bb(attck_sq));

            if (x != NULL_SQUARE) {
                return 0;
            }
        }

        value = std::max(piece_value_table[cur_piece] - see(pos, sq, opponent(side), piece, occupancy ^ sq_to_bb(attck_sq)), 0);
    }

    return value;
}

static int32_t capture_see(const Position& pos, Move mv) {
    Piece captured = move_captured_piece(mv);

    if (captured == PIECE_NONE) {
        return 0;
    }

    uint64_t occ = pos.all_pieces();

    occ ^= sq_to_bb(move_from(mv));
    occ ^= sq_to_bb(move_captured_square(mv));
    occ ^= sq_to_bb(move_to(mv));

    int sq = move_to(mv);

    Piece initial_piece = Piece(pos.piece_at[move_from(mv)]);
    int32_t value = piece_value_table[captured] - see(pos, sq, opponent(pos.to_move), initial_piece, occ);

    return value;
}

static int32_t score_quiet(Position& pos, Move mv) {
    (void)pos;
    (void)mv;
    return 0;
}

static int32_t score_capture(Position& pos, Move mv) {
    Piece captured = move_captured_piece(mv);
    Piece moving = Piece(pos.piece_at[move_from(mv)]);
    return piece_value_table[captured]*100 - moving;
}

static MoveScores score_moves(Position& pos, const MoveList& moves) {
    MoveScores scores;

    for (int i = 0; i < moves.count; ++i) {
        Move mv = moves.data[i];

        int32_t s;

        if (move_captured_piece(mv) == PIECE_NONE) {
            s = score_quiet(pos, mv);
        }
        else {
            s = score_capture(pos, mv);
        }

        scores.data[i] = s;
    }

    return scores;
}

static Move select_move(MoveList& moves, MoveScores& scores, int index) {
    int32_t best_score = -INF_SCORE;
    int best_index = -1;

    for (int i = index; i < moves.count; ++i) {
        if (scores.data[i] > best_score) {
            best_score = scores.data[i];
            best_index = i;
        }
    }

    std::swap(moves.data[index], moves.data[best_index]);
    std::swap(scores.data[index], scores.data[best_index]);

    return moves.data[index];
} 

static int32_t qsearch(Position& pos, SearchContext& s, int ply, int32_t alpha, int32_t beta) {
    s.qnode_count++;

    if (s.exit_on_node()) {
        return 0;
    }

    MoveList moves;

    int32_t best_score = -INF_SCORE;
    
    if (pos.is_checked[pos.to_move]) {
        moves = pos.generate_moves();
    }
    else {
        moves = pos.generate_captures();
        best_score = pos.signed_eval(); // we can "stand-pat" because we aren't in check
    }

    pos.filter_moves(moves);

    if (moves.count == 0 && pos.is_checked[pos.to_move]) {
        return -MATE_SCORE + ply;
    }

    MoveScores move_scores = score_moves(pos, moves);

    for (int move_index = 0; move_index < moves.count; ++move_index) {
        Move mv = select_move(moves, move_scores, move_index);

        if (!pos.is_checked[pos.to_move] && capture_see(pos, mv) < 0) {
            continue; // skip bad captures
        }

        pos.make_move(mv);

        int32_t score = -qsearch(pos, s, ply+1, -beta, -alpha);

        if (score > best_score) {
            best_score = score;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (alpha >= beta) {
            s.beta_cutoff_index_sum += move_index;
            s.beta_cutoff_count++;

            pos.unmake_move();
            return best_score;
        }

        pos.unmake_move();
    }

    return best_score;
}

static int32_t search(Position& pos, SearchContext& s, int depth, int ply, int32_t alpha, int32_t beta) {
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
        return qsearch(pos, s, ply, alpha, beta);
    }

    MoveScores move_scores = score_moves(pos, moves);

    int32_t best_score = -INF_SCORE;

    for (int move_index = 0; move_index < moves.count; ++move_index) {
        Move mv = select_move(moves, move_scores, move_index);
        pos.make_move(mv);

        int32_t score = -search(pos, s, depth-1, ply+1, -beta, -alpha);

        if (score > best_score) {
            best_score = score;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (alpha >= beta) {
            s.beta_cutoff_index_sum += move_index;
            s.beta_cutoff_count++;

            pos.unmake_move();
            return best_score;
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

    int32_t alpha = -INF_SCORE;
    int32_t beta  = INF_SCORE;

    for (Move mv : moves) {
        pos.make_move(mv);

        int32_t score = -search(pos, s, depth-1, 1, -beta, -alpha);

        if (score > best_score) {
            best_score = score;
            best_move = mv;
        }

        if (score > alpha) {
            alpha = score;
        }

        pos.unmake_move();
    }

    return {
        best_move,
        best_score
    };
}

Move Position::best_move(int depth, std::atomic<bool>& should_stop, Budgeter* budgeter, bool enable_uci_info, int64_t* score_out, SearchStatistics* stats /*optional*/) {
    (void)enable_uci_info;
    (void)score_out;

    budgeter->init();

    TimePoint start = Clock::now();

    SearchContext s = {
        .node_count = 0,
        .budgeter = budgeter,
        .should_stop = should_stop,
        .exited = false
    };

    Move best_move = NULL_MOVE;
    
    for (int d = 1; d <= depth; ++d) {
        auto [mv, score] = ::best_move(*this, s, d);

        if (s.exited) {
            break;
        }

        if (enable_uci_info) {
            std::string score_info;

            if (std::abs(score) > (MATE_SCORE - 1000)) {
                int mate_in = (MATE_SCORE - std::abs(score) + 1) / 2;
                score_info = std::format("mate {}{}", score < 0 ? "-" : "", mate_in);
            }
            else {
                score_info = std::format("cp {}", score);
            }

            TimePoint now = Clock::now();
            long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            auto seconds = double(std::chrono::duration_cast<std::chrono::microseconds>(now - start).count()) / 1000000;

            int nps = int(float(s.node_count) / float(seconds));

            print("info depth {} time {} nodes {} nps {} score {} pv {}\n", d, ms, s.node_count, nps, score_info, to_uci_move(mv));
        }

        best_move = mv;
    }

    assert(best_move != NULL_MOVE);

    if (stats) {
        stats->nodes = s.node_count;
        stats->qnodes = s.qnode_count;
        stats->time = float(double(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count())/1000000.0);
        stats->mean_cutoff_index = float(s.beta_cutoff_index_sum)/float(s.beta_cutoff_count);
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