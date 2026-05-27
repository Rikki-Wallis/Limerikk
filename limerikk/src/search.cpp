#include <array>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <memory>

#include "limerikk.h"

constexpr int32_t HASH_MOVE_SCORE       = 5000000;
constexpr int32_t GOOD_CAPTURE_SCORE    = 2000000;
constexpr int32_t PROMOTION_SCORE       = 2000000;
constexpr int32_t NEUTRAL_CAPTURE_SCORE = 1000000;
constexpr int32_t KILLER_SCORE          =  500000;
constexpr int32_t QUIET_SCORE           =  200000;
constexpr int32_t MAX_HISTORY           =   50000; 
constexpr int32_t BAD_CAPTURE_SCORE     =       0;

void ContinuationTable::update(int side, Piece piece, int to, int32_t bonus) {
    int32_t clamped_bonus = std::clamp(bonus, -MAX_HISTORY, MAX_HISTORY);
    table[side][piece][to] += clamped_bonus - table[side][piece][to] * std::abs(clamped_bonus) / MAX_HISTORY;
}

void TTEntry::write(uint64_t hash, TTType ty, Move move, int32_t score, int depth, int ply) {
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

int32_t TTEntry::score(int ply) {
    if (_score < -MATE_SCORE + 1000) {
        return _score + ply;
    } 

    if (_score > MATE_SCORE - 1000) {
        return _score - ply;
    }

    return _score;
}


void SearchContext::register_killer(int ply, Move mv) {
    if (mv != killers[ply][0] && mv != killers[ply][1]) {
        killers[ply][0] = killers[ply][1];
        killers[ply][1] = mv;
    }
}

void SearchContext::update_histories(int side, Piece piece, int to, int16_t bonus, SearchEntry* ss, int ply) {
    history.update(side, piece, to, bonus);

    for (int offset : {1, 2, 4}) {
        if (ply > offset && (ss-offset)->cont_hist) {
            (ss-offset)->cont_hist->update(side, piece, to, bonus);
        }
    }
}

bool SearchContext::exit_on_node() {
    if (should_stop) {
        exited = true;
    }

    metrics->node_count++;

    if ((metrics->node_count & 0xfff) == 0) {
        if (budgeter->should_exit(metrics->node_count)) {
            exited = true;
        }
    }

    return exited;
}


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

static int32_t score_quiet(Position& pos, SearchContext& s, Move mv, int ply, SearchEntry* ss) {
    Piece piece = Piece(pos.piece_at[move_from(mv)]);
    int side = move_side(mv);
    int to = move_to(mv);

    int32_t score = s.history.table[side][piece][to];

    for (int i : {1, 2, 4}) {
        if (ply > i && (ss-i)->cont_hist) {
            score += (ss-i)->cont_hist->table[side][piece][to];
        }
    }

    return score;
}

static int32_t score_capture(Position& pos, Move mv) {
    Piece captured = move_captured_piece(mv);
    Piece moving = Piece(pos.piece_at[move_from(mv)]);
    return piece_value_table[captured]*10 - piece_value_table[moving];
}

static MoveScores score_moves(Position& pos, SearchContext& s, const MoveList& moves, int ply, Move hash_move, SearchEntry* ss) {
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
            x = score_quiet(pos, s, mv, ply, ss) + QUIET_SCORE;
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

static void push_move(SearchContext& s, Position& pos, Move mv, SearchEntry* ss) {
    ContinuationTable* table = nullptr;

    if (mv != NULL_MOVE) {
        Piece piece = Piece(pos.piece_at[move_from(mv)]);
        int to = move_to(mv);
        int side = move_side(mv);
        table = &s.cont_history[side][piece][to];
    }

    if (mv == NULL_MOVE) {
        pos.make_null_move();
    }
    else {
        pos.make_move(mv);
    }

    *ss = {
        .cont_hist = table,
        .move = mv
    };
}

static void pop_move(Position& pos, SearchEntry* ss) {
    if (ss->move == NULL_MOVE) {
        pos.unmake_null_move();
    }
    else {
        pos.unmake_move();
    }
}

static int32_t qsearch(Position& pos, SearchContext& s, int ply, int32_t alpha, int32_t beta, SearchEntry* ss) {
    int side = pos.to_move;

    s.metrics->qnode_count++;

    int32_t alpha0 = alpha;
    //int32_t beta0  = beta;

    bool pv_node = beta > alpha + 1;

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
        if (!pv_node && check_tt_cutoff(tt_entry, alpha, beta, 0, ply, &hash_score)){
            return hash_score;
        }
    }



    MoveScores move_scores = score_moves(pos, s, moves, ply, hash_move, ss);
    Move best_move = NULL_MOVE;

    int legal_move_index = 0;

    for (int move_index = 0; move_index < moves.count; ++move_index) {
        Move mv = select_move(moves, move_scores, move_index);

        bool quiet = move_captured_piece(mv) == PIECE_NONE;

        if (!pos.is_checked[pos.to_move] && capture_see(pos, mv) < 0) {
            continue; // skip bad captures
        }

        push_move(s, pos, mv, ss);

        if (pos.is_checked[side]) {
            pop_move(pos, ss);
            continue;
        }

        int32_t score = -qsearch(pos, s, ply+1, -beta, -alpha, ss+1);

        if (score > best_score) {
            best_score = score;
        }

        if (score > alpha) {
            alpha = score;
            best_move = mv;
        }

        if (alpha >= beta) {
            s.metrics->beta_cutoff_index_sum += move_index;
            s.metrics->beta_cutoff_count++;

            if (quiet) {
                s.register_killer(ply, mv);
            }

            pop_move(pos, ss);

            tt_entry.write(pos.zobrist, TT_CUT, mv, score, 0, ply);

            return best_score;
        }

        legal_move_index++;

        pop_move(pos, ss);
    }

    if (legal_move_index == 0 && pos.is_checked[pos.to_move]) {
        return -MATE_SCORE + ply;
    }

    tt_entry.write(pos.zobrist, best_score > alpha0 ? TT_PV : TT_ALL, best_move, best_score, 0, ply);

    return best_score;
}

static int32_t search(Position& pos, SearchContext& s, int depth, int ply, int32_t alpha, int32_t beta, SearchEntry* ss, Move* best_move_out = nullptr, bool allow_null_move = true) {
    int side = pos.to_move;
    bool in_check = pos.is_checked[side];

    int32_t alpha0 = alpha;
    //int32_t beta0  = beta;

    s.metrics->sel_depth = std::max(s.metrics->sel_depth, ply);

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
        return qsearch(pos, s, ply, alpha, beta, ss);
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
        if (!pv_node && check_tt_cutoff(tt_entry, alpha, beta, depth, ply, &hash_score)) {
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
        s.metrics->nmp_attempts++;

        int r = 3;

        push_move(s, pos, NULL_MOVE, ss);
        int32_t score = -search(pos, s, depth-1-r, ply+1, -beta, -(beta-1), ss+1, nullptr, false);
        pop_move(pos, ss);

        if (score >= beta) {
            s.metrics->nmp_cutoffs++;
            return score;
        }
    }



    MoveList moves = pos.generate_moves();
    MoveScores move_scores = score_moves(pos, s, moves, ply, hash_move, ss);

    int32_t best_score = -INF_SCORE;
    Move best_move = NULL_MOVE;

    Move quiets[256];
    int quiet_count = 0;

    int legal_move_index = 0;

    for (int move_index = 0; move_index < moves.count; ++move_index) {
        Move mv = select_move(moves, move_scores, move_index);

        Piece piece = Piece(pos.piece_at[move_from(mv)]);
        int to = move_to(mv);

        push_move(s, pos, mv, ss);

        if (pos.is_checked[side]) {
            pop_move(pos, ss);
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

            s.metrics->lmr_count++;
            s.metrics->lmr_sum += lmr;
        }


        // perform principal variation search

        int32_t score;

        if (!pv_node || legal_move_index > 0) {
            score = -search(pos, s, depth-1-lmr, ply+1, -(alpha+1), -alpha, ss+1);

            s.metrics->reduced_searches += lmr > 0;

            if (lmr > 0 && score > alpha) {
                s.metrics->reduced_re_searches++;
                score = -search(pos, s, depth-1, ply+1, -(alpha+1), -alpha, ss+1);
            }
        }
        if (pv_node && (legal_move_index == 0 || score > alpha)) {
            score = -search(pos, s, depth-1, ply+1, -beta, -alpha, ss+1);
        }



        if (score > best_score) {
            best_score = score;
        }

        if (score > alpha) {
            alpha = score;
            best_move = mv;
        }

        if (alpha >= beta) {
            s.metrics->beta_cutoff_index_sum += move_index;
            s.metrics->beta_cutoff_count++;

            int16_t hist_bonus = 300 * int16_t(depth) - 250;

            pop_move(pos, ss);

            if (quiet) {
                s.register_killer(ply, mv);

                s.update_histories(side, piece, to, hist_bonus, ss, ply);

                for (int i = quiet_count-2; i >= 0; --i) {
                    Piece malus_piece = Piece(pos.piece_at[move_from(quiets[i])]);
                    int malus_to = move_to(quiets[i]);

                    s.update_histories(side, malus_piece, malus_to, int16_t(-hist_bonus), ss, ply);
                }
            }

            tt_entry.write(pos.zobrist, TT_CUT, mv, score, depth, ply);

            return best_score;
        }

        legal_move_index++;

        pop_move(pos, ss);
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

Move Position::best_move(SearchContext& s, int depth, bool enable_uci_info, int64_t* score_out, SearchStatistics* stats) {
    (void)score_out;

    TimePoint start = Clock::now();

    s.budgeter->init();

    SearchEntry search_stack[MAX_DEPTH];
    SearchEntry* ss = search_stack;

    SearchMetrics metrics{};
    s.metrics = &metrics;
    s.exited = false;

    Move best_move = NULL_MOVE;

    int32_t window_center;
    int expansions = 0;
    
    for (int d = 1; d <= depth; ++d) {
        if (!s.budgeter->should_start_next_iteration()) {
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

            score = search(*this, s, d, 0, alpha, beta, ss, &mv);

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

            int nps = int(float(metrics.node_count) / float(seconds));

            print("info depth {} seldepth {} time {} nodes {} nps {} score {} pv {}\n", d, metrics.sel_depth, ms, metrics.node_count, nps, score_info, to_uci_move(mv));
        }

        best_move = mv;
    }

    assert(best_move != NULL_MOVE);

    if (stats) {
        stats->nodes = metrics.node_count;
        stats->qnodes = metrics.qnode_count;
        stats->time = float(double(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count())/1000000.0);
        stats->mean_cutoff_index = float(metrics.beta_cutoff_index_sum)/float(metrics.beta_cutoff_count);
        stats->tt_hit_rate = float(metrics.tt_hits)/float(metrics.tt_attempts);
        stats->nmp_cutoff_rate = float(metrics.nmp_cutoffs)/float(metrics.nmp_attempts);
        stats->mean_lmr = float(metrics.lmr_sum)/float(metrics.lmr_count);
        stats->expansions = depth >= 4 ? float(expansions)/float(depth-3) : 0.0f;
        stats->sel_depth = metrics.sel_depth;
        stats->reduced_re_search_rate = float(metrics.reduced_re_searches)/float(metrics.reduced_searches);
    }

    s.metrics = nullptr;

    return best_move;
}

Move Position::think(SearchContext& s, int depth, bool enable_uci_info, int64_t* score_out) {
    uint64_t hash = encode_polyglot();
    std::vector<PolyglotEntry> p_moves = probe_book(hash);

    if (!p_moves.empty()) {
        PolyglotEntry line = choose_move(p_moves);
        Move move = decode_polyglot(line);
        assert(is_move_legal_slow(move));
        return move;
    } else {
        Move best = best_move(s, depth, enable_uci_info, score_out);
        assert(best != NULL_MOVE);
        return best;
    }
}