#include <array>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <memory>

#include "limerikk.h"

constexpr int32_t HASH_MOVE_SCORE       = 500000;
constexpr int32_t GOOD_CAPTURE_SCORE    = 200000;
constexpr int32_t PROMOTION_SCORE       = 200000;
constexpr int32_t NEUTRAL_CAPTURE_SCORE = 100000;
constexpr int32_t KILLER_SCORE          =  50000;
constexpr int32_t QUIET_SCORE           =  20000;
constexpr int32_t MAX_HISTORY           =   5000; 
constexpr int32_t BAD_CAPTURE_SCORE     =      0;


struct TTEntry {
    uint64_t hash;
    Move best_move;
};

constexpr size_t TT_SIZE = (1 << 20);
constexpr uint64_t TT_MASK = TT_SIZE - 1;

struct SearchContext {
    int node_count = 0;
    int qnode_count = 0;

    int beta_cutoff_index_sum = 0;
    int beta_cutoff_count = 0;

    int tt_attempts = 0;
    int tt_hits = 0;

    int nmp_attempts = 0;
    int nmp_cutoffs = 0;

    Budgeter* budgeter;
    std::atomic<bool>& should_stop;

    bool exited = false;

    Move killers[MAX_DEPTH][2] = {};
    TTEntry tt[TT_SIZE]{};
    int32_t history[2][NUM_PIECE_TYPES][64] = {};

    SearchContext(Budgeter* budgeter, std::atomic<bool>& should_stop)
        : budgeter(budgeter), should_stop(should_stop)
    {
    }

    void register_killer(int ply, Move mv) {
        if (mv != killers[ply][0] && mv != killers[ply][1]) {
            killers[ply][0] = killers[ply][1];
            killers[ply][1] = mv;
        }
    }

    void register_history(int side, Piece piece, int to, int32_t bonus) {
        int32_t clamped_bonus = std::clamp(bonus, -MAX_HISTORY, MAX_HISTORY);
        history[side][piece][to] += clamped_bonus - history[side][piece][to] * std::abs(clamped_bonus) / MAX_HISTORY;
    }

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

    TTEntry& tt_query(uint64_t hash) {
        size_t index = hash & TT_MASK;

        TTEntry& e = tt[index];

        tt_attempts++;
        tt_hits += e.hash == hash;

        return e;
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

static int32_t score_quiet(Position& pos, SearchContext& s, Move mv) {
    Piece piece = Piece(pos.piece_at[move_from(mv)]);
    return s.history[pos.to_move][piece][move_to(mv)];
}

static int32_t score_capture(Position& pos, Move mv) {
    Piece captured = move_captured_piece(mv);
    Piece moving = Piece(pos.piece_at[move_from(mv)]);
    return piece_value_table[captured] - moving;
}

static MoveScores score_moves(Position& pos, SearchContext& s, const MoveList& moves, int ply, Move hash_move) {
    MoveScores scores;

    for (int i = 0; i < moves.count; ++i) {
        Move mv = moves.data[i];

        bool quiet = move_captured_piece(mv) == PIECE_NONE;
        bool promotion = move_type(mv) == MOVE_PROMOTION;

        int32_t x;

        if (mv == hash_move) {
            x = HASH_MOVE_SCORE;
        }
        else if (quiet && promotion) {
            x = PROMOTION_SCORE;
        }
        else if (mv == s.killers[ply][0] || mv == s.killers[ply][1]) {
            x = KILLER_SCORE;
        }
        else if (quiet) {
            x = score_quiet(pos, s, mv) + QUIET_SCORE;
        }
        else {
            int32_t see_score = capture_see(pos, mv);

            int32_t base = 0;

            if (see_score > 0) {
                base = promotion ? PROMOTION_SCORE : GOOD_CAPTURE_SCORE;
            }
            else if (see_score == 0) {
                base = NEUTRAL_CAPTURE_SCORE;
            }
            else {
                base = BAD_CAPTURE_SCORE;
            }

            x = score_capture(pos, mv) + base;
        }

        scores.data[i] = x;
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
    int side = pos.to_move;

    s.qnode_count++;

    if (s.exit_on_node()) {
        return 0;
    }

    if (pos.is_threefold_repetition()) {
        return 0;
    }

    if (pos.half_move_clock == 100) {
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

    if (best_score >= beta) {
        return best_score;
    }

    TTEntry& tt_entry = s.tt_query(pos.zobrist);

    Move hash_move = NULL_MOVE;

    if (tt_entry.hash == pos.zobrist) {
        hash_move = tt_entry.best_move;
    }

    MoveScores move_scores = score_moves(pos, s, moves, ply, hash_move);
    Move best_move = NULL_MOVE;

    int legal_move_index = 0;

    for (int move_index = 0; move_index < moves.count; ++move_index) {
        Move mv = select_move(moves, move_scores, move_index);

        bool quiet = move_captured_piece(mv) == PIECE_NONE;

        if (!pos.is_checked[pos.to_move] && capture_see(pos, mv) < 0) {
            continue; // skip bad captures
        }

        pos.make_move(mv);

        if (!pos.is_checked[side]) {
            int32_t score = -qsearch(pos, s, ply+1, -beta, -alpha);

            if (score > best_score) {
                best_score = score;
                best_move = mv;
            }

            if (score > alpha) {
                alpha = score;
            }

            if (alpha >= beta) {
                s.beta_cutoff_index_sum += move_index;
                s.beta_cutoff_count++;

                if (quiet) {
                    s.register_killer(ply, mv);
                }

                pos.unmake_move();
                return best_score;
            }

            legal_move_index++;
        }

        pos.unmake_move();
    }

    if (legal_move_index == 0 && pos.is_checked[pos.to_move]) {
        return -MATE_SCORE + ply;
    }

    tt_entry.hash = pos.zobrist;
    tt_entry.best_move = best_move;

    return best_score;
}

static int32_t search(Position& pos, SearchContext& s, int depth, int ply, int32_t alpha, int32_t beta, Move* best_move_out = nullptr, bool allow_null_move = true) {
    int side = pos.to_move;

    if (best_move_out) {
        *best_move_out = NULL_MOVE;
    }

    if (s.exit_on_node()) {
        return 0;
    }

    if (pos.is_threefold_repetition()) {
        return 0;
    }

    if (pos.half_move_clock == 100) {
        return 0;
    }

    if (depth <= 0) {
        return qsearch(pos, s, ply, alpha, beta);
    }

    // Transposition table lookup
    TTEntry& tt_entry = s.tt_query(pos.zobrist);

    Move hash_move = NULL_MOVE;

    if (tt_entry.hash == pos.zobrist) {
        hash_move = tt_entry.best_move;
    }



    // reverse futility pruning
    // if our static evaluation is greater than beta by a margin
    // assume we will fail-high

    int rfp_margin = 150 * depth;

    if (!pos.is_checked[side] &&
        pos.signed_eval() >= beta + rfp_margin &&
        std::abs(beta) < MATE_SCORE - 1000
    ) {
        return pos.signed_eval();
    }
    

    // null move pruning
    // if we skip our turn and still beat beta
    // any legal move will likely fail high

    if (
        allow_null_move && 
        !pos.is_checked[side] &&
        pos.non_pawn_material() > 0 && // prevent NMP in zugzwang positions (mostly pawn endgames)
        std::abs(beta) < MATE_SCORE - 1000 &&
        depth >= 4
    ) {
        s.nmp_attempts++;

        int r = 3;

        pos.make_null_move();
        int32_t score = -search(pos, s, depth-1-r, ply+1, -beta, -(beta-1), nullptr, false);
        pos.unmake_null_move();

        if (score >= beta) {
            s.nmp_cutoffs++;
            return score;
        }
    }



    MoveList moves = pos.generate_moves();
    MoveScores move_scores = score_moves(pos, s, moves, ply, hash_move);

    int32_t best_score = -INF_SCORE;
    Move best_move = NULL_MOVE;

    Move quiets[256];
    int quiet_count = 0;

    int legal_move_index = 0;

    for (int move_index = 0; move_index < moves.count; ++move_index) {
        Move mv = select_move(moves, move_scores, move_index);

        bool quiet = move_captured_piece(mv) == PIECE_NONE;

        if (quiet) {
            quiets[quiet_count++] = mv;
        }

        Piece piece = Piece(pos.piece_at[move_from(mv)]);
        int to = move_to(mv);

        pos.make_move(mv);

        if (!pos.is_checked[side]) {
            int32_t score = -search(pos, s, depth-1, ply+1, -beta, -alpha);

            if (score > best_score) {
                best_score = score;
                best_move = mv;
            }

            if (score > alpha) {
                alpha = score;
            }

            if (alpha >= beta) {
                s.beta_cutoff_index_sum += move_index;
                s.beta_cutoff_count++;

                int hist_bonus = 300 * depth - 250;

                if (quiet) {
                    s.register_killer(ply, mv);
                    s.register_history(side, piece, to, hist_bonus);

                    for (int i = quiet_count-2; i >= 0; --i) {
                        s.register_history(side, piece, to, -hist_bonus);
                    }
                }

                pos.unmake_move();
                return best_score;
            }

            legal_move_index++;
        }

        pos.unmake_move();
    }

    if (legal_move_index == 0) {
        if (pos.is_checked[pos.to_move]) {
            return -MATE_SCORE + ply;
        }
        else {
            return 0;
        }
    }

    tt_entry.hash = pos.zobrist;
    tt_entry.best_move = best_move;

    if (best_move_out) {
        *best_move_out = best_move;
    }

    return best_score;
}

Move Position::best_move(int depth, std::atomic<bool>& should_stop, Budgeter* budgeter, bool enable_uci_info, int64_t* score_out, SearchStatistics* stats /*optional*/) {
    (void)enable_uci_info;
    (void)score_out;

    budgeter->init();

    TimePoint start = Clock::now();

    auto s = std::make_unique<SearchContext>(budgeter, should_stop);

    Move best_move = NULL_MOVE;
    
    for (int d = 1; d <= depth; ++d) {
        Move mv;
        int32_t score = search(*this, *s, d, 0, -INF_SCORE, INF_SCORE, &mv);

        if (s->exited) {
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

            int nps = int(float(s->node_count) / float(seconds));

            print("info depth {} time {} nodes {} nps {} score {} pv {}\n", d, ms, s->node_count, nps, score_info, to_uci_move(mv));
        }

        best_move = mv;
    }

    assert(best_move != NULL_MOVE);

    if (stats) {
        stats->nodes = s->node_count;
        stats->qnodes = s->qnode_count;
        stats->time = float(double(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count())/1000000.0);
        stats->mean_cutoff_index = float(s->beta_cutoff_index_sum)/float(s->beta_cutoff_count);
        stats->tt_hit_rate = float(s->tt_hits)/float(s->tt_attempts);
        stats->nmp_cutoff_rate = float(s->nmp_cutoffs)/float(s->nmp_attempts);
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