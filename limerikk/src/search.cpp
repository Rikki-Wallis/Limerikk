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

enum TTType {
    TT_PV,
    TT_ALL,
    TT_CUT
};

struct TTEntry {
    uint64_t hash;
    TTType type;
    Move best_move;
    int32_t _score;
    int depth;

    void write(uint64_t hash, TTType ty, Move move, int32_t score, int depth, int ply) {
        this->hash = hash;
        this->type = ty;
        this->best_move = move;
        this->depth = depth;

        // adjust mate scores to be relative to the current node, rather than the root

        if (score < -MATE_SCORE + 1000) {
            score -= ply;
        }
        else if (score > MATE_SCORE - 1000) {
            score += ply;
        }

        this->_score = score;
    }

    int32_t score(int ply) {
        if (_score < -MATE_SCORE + 1000) {
            return _score + ply;
        } 

        if (_score > MATE_SCORE - 1000) {
            return _score - ply;
        }

        return _score;
    }
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

    int lmr_count = 0;
    int lmr_sum = 0;

    int sel_depth = 0;

    int reduced_searches = 0;
    int reduced_re_searches = 0;

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

    template<bool Stats>
    TTEntry& tt_query(uint64_t hash) {
        size_t index = hash & TT_MASK;

        TTEntry& e = tt[index];

        if constexpr (Stats) {
            tt_attempts++;
            tt_hits += e.hash == hash;
        }

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

static bool check_tt_cutoff(TTEntry& tt_entry, int32_t alpha, int32_t beta, int depth, int ply, int32_t* score_out) {
    if (tt_entry.depth >= depth) {
        int32_t hash_score = tt_entry.score(ply);
        *score_out = hash_score;

        switch (tt_entry.type) {
            case TT_PV: // exact score
                return true;
            case TT_ALL: // upper-bound
                if (hash_score <= alpha) {
                    return true; // fail-low
                }
                break;
            case TT_CUT: // lower-bound
                if (hash_score >= beta) {
                    return true; // fail-high
                }
                break;
        }
    }

    return false;
}

static int32_t qsearch(Position& pos, SearchContext& s, int ply, int32_t alpha, int32_t beta) {
    int side = pos.to_move;

    s.qnode_count++;

    int32_t alpha0 = alpha;
    //int32_t beta0  = beta;

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


    TTEntry& tt_entry = s.tt_query<false>(pos.zobrist);

    Move hash_move = NULL_MOVE;

    if (tt_entry.hash == pos.zobrist) {
        hash_move = tt_entry.best_move;

        int32_t hash_score;
        if (check_tt_cutoff(tt_entry, alpha, beta, 0, ply, &hash_score)){
            return hash_score;
        }
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

        if (pos.is_checked[side]) {
            pos.unmake_move();
            continue;
        }

        int32_t score = -qsearch(pos, s, ply+1, -beta, -alpha);

        if (score > best_score) {
            best_score = score;
        }

        if (score > alpha) {
            alpha = score;
            best_move = mv;
        }

        if (alpha >= beta) {
            s.beta_cutoff_index_sum += move_index;
            s.beta_cutoff_count++;

            if (quiet) {
                s.register_killer(ply, mv);
            }

            pos.unmake_move();

            tt_entry.write(pos.zobrist, TT_CUT, mv, score, 0, ply);

            return best_score;
        }

        legal_move_index++;

        pos.unmake_move();
    }

    if (legal_move_index == 0 && pos.is_checked[pos.to_move]) {
        return -MATE_SCORE + ply;
    }

    tt_entry.write(pos.zobrist, best_score > alpha0 ? TT_PV : TT_ALL, best_move, best_score, 0, ply);

    return best_score;
}

static int32_t search(Position& pos, SearchContext& s, int depth, int ply, int32_t alpha, int32_t beta, Move* best_move_out = nullptr, bool allow_null_move = true) {
    int side = pos.to_move;
    bool in_check = pos.is_checked[side];

    int32_t alpha0 = alpha;
    //int32_t beta0  = beta;

    s.sel_depth = std::max(s.sel_depth, ply);

    bool pv_node = beta > alpha + 1;

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



    // transposition table lookup
    // we use the hash move for move ordering
    // and the stored score for bounds adjustments
    // and cutoffs

    TTEntry& tt_entry = s.tt_query<true>(pos.zobrist);

    Move hash_move = NULL_MOVE;

    if (tt_entry.hash == pos.zobrist) {
        hash_move = tt_entry.best_move;

        int32_t hash_score;
        if (check_tt_cutoff(tt_entry, alpha, beta, depth, ply, &hash_score)) {
            if (tt_entry.type == TT_PV && best_move_out) {
                *best_move_out = hash_move;
            }
            return hash_score;
        }
    }



    // reverse futility pruning
    // if our static evaluation is greater than beta by a margin
    // assume we will fail-high

    int rfp_margin = 150 * depth;

    if (!pv_node &&
        !in_check &&
        pos.signed_eval() >= beta + rfp_margin &&
        std::abs(beta) < MATE_SCORE - 1000
    ) {
        return pos.signed_eval();
    }




    // null move pruning
    // if we skip our turn and still beat beta
    // any legal move will likely fail high

    if (
        !pv_node &&
        allow_null_move && 
        !in_check &&
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

        Piece piece = Piece(pos.piece_at[move_from(mv)]);
        int to = move_to(mv);

        pos.make_move(mv);

        if (pos.is_checked[side]) {
            pos.unmake_move();
            continue;
        }

        bool quiet = move_captured_piece(mv) == PIECE_NONE;
        //bool gives_check = pos.is_checked[opponent(side)];
        //bool promotion = move_type(mv) == MOVE_PROMOTION;

        if (quiet) {
            quiets[quiet_count++] = mv;
        }

        // calculate the late move reduction
        // the idea is that late moves are not likely to cause a beta-cutoff, so we can reduce
        // their search depth

        int lmr = 0;
        if (
            depth >= 4 &&
            legal_move_index > 2 &&
            !in_check
        ) {
            float lmr_frac = 0.5f + std::log(float(depth)) * std::log(float(legal_move_index)) / 3.0f;
            lmr = std::max(int(std::round(lmr_frac)), 0);

            s.lmr_count++;
            s.lmr_sum += lmr;
        }


        // perform principal variation search

        int32_t score;

        if (!pv_node || legal_move_index > 0) {
            score = -search(pos, s, depth-1-lmr, ply+1, -(alpha+1), -alpha);

            s.reduced_searches += lmr > 0;

            if (lmr > 0 && score > alpha) {
                s.reduced_re_searches++;
                score = -search(pos, s, depth-1, ply+1, -(alpha+1), -alpha);
            }
        }
        if (pv_node && (legal_move_index == 0 || score > alpha)) {
            score = -search(pos, s, depth-1, ply+1, -beta, -alpha);
        }



        if (score > best_score) {
            best_score = score;
        }

        if (score > alpha) {
            alpha = score;
            best_move = mv;
        }

        if (alpha >= beta) {
            s.beta_cutoff_index_sum += move_index;
            s.beta_cutoff_count++;

            int hist_bonus = 300 * depth - 250;

            pos.unmake_move();

            if (quiet) {
                s.register_killer(ply, mv);
                s.register_history(side, piece, to, hist_bonus);

                for (int i = quiet_count-2; i >= 0; --i) {
                    Piece malus_piece = Piece(pos.piece_at[move_from(quiets[i])]);
                    int malus_to = move_to(quiets[i]);
                    s.register_history(side, malus_piece, malus_to, -hist_bonus);
                }
            }

            tt_entry.write(pos.zobrist, TT_CUT, mv, score, depth, ply);

            return best_score;
        }

        legal_move_index++;

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

    tt_entry.write(pos.zobrist, best_score > alpha0 ? TT_PV : TT_ALL, best_move, best_score, depth, ply);

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

    int32_t window_center;
    int expansions = 0;
    
    for (int d = 1; d <= depth; ++d) {
        if (!s->budgeter->should_start_next_iteration()) {
            break;
        }

        Move mv;
        int32_t score;

        int32_t window_lo = 25;
        int32_t window_hi = 25;
        
        for (;;) {
            int32_t alpha = window_center - window_lo;
            int32_t beta  = window_center + window_hi;

            if (d < 4) {
                alpha = -INF_SCORE;
                beta = INF_SCORE;
            }

            alpha = std::clamp(alpha, -INF_SCORE, INF_SCORE);
            beta = std::clamp(beta, -INF_SCORE, INF_SCORE);

            score = search(*this, *s, d, 0, alpha, beta, &mv);

            if (score > alpha && score < beta) {
                break;
            }
            else if (score >= beta) {
                window_hi *= 4;
            }
            else if (score <= alpha) {
                window_lo *= 4;
            }

            expansions++;
        }

        window_center = score;

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

            print("info depth {} seldepth {} time {} nodes {} nps {} score {} pv {}\n", d, s->sel_depth, ms, s->node_count, nps, score_info, to_uci_move(mv));
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
        stats->mean_lmr = float(s->lmr_sum)/float(s->lmr_count);
        stats->expansions = depth >= 4 ? float(expansions)/float(depth-3) : 0.0f;
        stats->sel_depth = s->sel_depth;
        stats->reduced_re_search_rate = float(s->reduced_re_searches)/float(s->reduced_searches);
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