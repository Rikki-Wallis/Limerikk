#include <array>
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <memory>

#include "limerikk.h"

// bruh

constexpr int32_t HASH_MOVE_SCORE = 300000;
constexpr int32_t CAPTURE_BASE    = 200000;
constexpr int32_t QUIET_MOVE_BASE = 100000;

constexpr int16_t MAX_HISTORY     = 30000;

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

TTEntry* SearchContext::tt_query(uint64_t hash) {
    size_t index = hash & TT_MASK;

    metrics->tt_queries++;

    if (tt[index].hash == hash) {
        metrics->tt_matches++;
        return &tt[index];
    }

    return nullptr;
}

void SearchContext::tt_write(uint64_t hash, Move move, int16_t score, int8_t depth, TTKind kind, int ply) {
    size_t index = hash & TT_MASK;

    int16_t rel_score = score;

    if (rel_score > MATE_SCORE - 1000) {
        rel_score += ply;
    }
    else if (rel_score < -MATE_SCORE + 1000) {
        rel_score -= ply;
    }

    TTEntry* e = &tt[index];

    *e = {
        .hash = hash,
        .move = move,
        .rel_score = rel_score,
        .depth = depth,
        .kind = kind
    };
}

int16_t TTEntry::score(int ply) const {
    if (rel_score > MATE_SCORE - 1000) {
        return rel_score - int16_t(ply);
    }
    else if (rel_score < -MATE_SCORE + 1000) {
        return rel_score + int16_t(ply);
    }
    else {
        return rel_score;
    }
}

bool TTEntry::cutoff(int d, int ply, int32_t alpha, int32_t beta) const {
    if (d > depth) {
        return false;
    }

    int16_t s = score(ply);

    switch (kind) {
        case TT_EXACT:
            return true; 
        case TT_LOWER:
            return s >= beta;
        case TT_UPPER:
            return s <= alpha;
    }

    return false;
}


void HistoryTable::update(int side, int from, int to, int16_t bonus) {
    int16_t b = std::clamp(bonus, int16_t(-MAX_HISTORY), MAX_HISTORY);
    data[side][from][to] += b - data[side][from][to] * std::abs(b) / MAX_HISTORY;
}

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

static void push_move(SearchContext& s, Position& pos, Move mv, SearchEntry* ss) {
    (void)s;

    if (mv == NULL_MOVE) {
        pos.make_null_move();
    }
    else {
        pos.make_move(mv);
    }

    *ss = {
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

struct MoveScores {
    int32_t data[256];
};

static int32_t mvv_lva(const Position& pos, Move mv) {
    Piece moving = (Piece)pos.piece_at[move_from(mv)];
    Piece captured = move_captured_piece(mv);

    return piece_value_table[captured]*100 - piece_value_table[moving];
}

static MoveScores score_moves(const Position& pos, SearchContext& s, const MoveList& moves, Move hash_move) {
    MoveScores scores{};

    for (int i = 0; i < moves.count; ++i) {
        Move mv = moves.data[i];
        bool quiet = move_captured_piece(mv) == PIECE_NONE;

        int32_t score = 0;

        if (mv == hash_move) {
            score = HASH_MOVE_SCORE;
        }
        else if (quiet) {
            score = QUIET_MOVE_BASE;
            score += s.history.data[move_side(mv)][move_from(mv)][move_to(mv)];
        }
        else {
            score = CAPTURE_BASE;
            score += mvv_lva(pos, mv);
        }

        scores.data[i] = score;
    }

    return scores;
}

static Move select_move(MoveList& moves, MoveScores& scores, int index) {
    int best_index = -1;
    int32_t best_score = INT32_MIN;

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

static int32_t qsearch(Position& pos, SearchContext& s, int ply, int32_t alpha, int32_t beta, SearchEntry* ss) {
    int32_t alpha0 = alpha;

    int side = pos.to_move;

    bool in_check = pos.is_checked[side];
    bool pv_node = beta > alpha + 1;

    s.metrics->qnode_count++;
    s.metrics->pv_node_count += pv_node;

    if (s.exit_on_node()) {
        return 0;
    }

    if (pos.is_threefold_repetition()) {
        return 0;
    }

    if (pos.half_move_clock == 100) {
        return 0;
    }



    Move hash_move = NULL_MOVE;

    {
        TTEntry* e = s.tt_query(pos.zobrist);

        if (e) {
            hash_move = e->move;

            if (!pv_node && e->cutoff(0, ply, alpha, beta)) {
                return e->score(ply);
            }
        }
    }



    MoveList moves;

    int32_t best_score = -INF_SCORE;
    
    if (in_check) {
        moves = pos.generate_moves();
    }
    else {
        moves = pos.generate_captures();
        best_score = pos.signed_eval(); // we can "stand-pat" because we aren't in check
    }

    if (best_score >= beta) {
        return best_score;
    }

    MoveScores move_scores = score_moves(pos, s, moves, hash_move);

    int legal_move_index = 0;
    Move best_move = NULL_MOVE;

    for (int move_index = 0; move_index < moves.count; ++move_index) {
        Move mv = select_move(moves, move_scores, move_index);

        bool quiet = move_captured_piece(mv) == PIECE_NONE;

        if (!in_check && !quiet && capture_see(pos, mv) < 0) {
            continue;
        }

        push_move(s, pos, mv, ss);

        if (pos.is_checked[side]) {
            pop_move(pos, ss); // illegal move
            continue;
        }

        int32_t score = -qsearch(pos, s, ply+1, -beta, -alpha, ss+1);

        if (s.exited) {
            pop_move(pos, ss);
            return 0;
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

            pop_move(pos, ss);

            s.tt_write(pos.zobrist, mv, int16_t(best_score), 0, TT_LOWER, ply);

            return best_score;
        }

        legal_move_index++;

        pop_move(pos, ss);
    }

    if (legal_move_index == 0 && pos.is_checked[pos.to_move]) {
        return -MATE_SCORE + ply;
    }

    s.tt_write(pos.zobrist, best_move, int16_t(best_score), 0, best_score > alpha0 ? TT_EXACT : TT_UPPER, ply);

    return best_score;
}

static int32_t search(Position& pos, SearchContext& s, int depth, int ply, int32_t alpha, int32_t beta, SearchEntry* ss, Move* best_move_out = nullptr, bool allow_null_move = true) {
    int32_t alpha0 = alpha;

    int side = pos.to_move;
    bool in_check = pos.is_checked[side];

    s.metrics->sel_depth = std::max(s.metrics->sel_depth, ply);

    bool pv_node = beta > alpha + 1;
    s.metrics->pv_node_count += pv_node;

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


    Move hash_move = NULL_MOVE;

    {
        TTEntry* e = s.tt_query(pos.zobrist);
        if (e) {
            hash_move = e->move;

            if (!pv_node && e->cutoff(depth, ply, alpha, beta)) {
                if (e->kind == TT_EXACT) {
                    if (best_move_out) {
                        *best_move_out = e->move;
                    }
                }
                return e->score(ply);
            }
        }
    }



    // reverse futility pruning

    bool can_rfp = !in_check && !pv_node;

    int rfp_margin = 150 * depth;

    if (can_rfp && pos.signed_eval() >= beta + rfp_margin) {
        s.metrics->rfp_count++;
        return pos.signed_eval();
    }




    // null move pruning

    bool can_nmp = depth > 3 && !in_check && pos.non_pawn_material() > 0 && !pv_node && allow_null_move;

    if (can_nmp) {
        push_move(s, pos, NULL_MOVE, ss);

        int r = 3 + depth / 4;
        int32_t v = -search(pos, s, depth-r-1, ply+1, -beta, -(beta-1), ss+1, nullptr, false);

        pop_move(pos, ss);

        if (v >= beta) {
            s.metrics->nmp_count++;
            return v;
        }
    }



    MoveList moves = pos.generate_moves();
    MoveScores move_scores = score_moves(pos, s, moves, hash_move);

    int32_t best_score = -INF_SCORE;
    Move best_move = NULL_MOVE;

    Move quiets[256];
    int quiet_count = 0;

    int legal_move_index = 0;

    for (int plmi = 0; plmi < moves.count; ++plmi) {
        Move mv = select_move(moves, move_scores, plmi);

        bool quiet = move_captured_piece(mv) == PIECE_NONE;

        if (quiet) {
            quiets[quiet_count++] = mv;
        }

        push_move(s, pos, mv, ss);

        if (pos.is_checked[side]) {
            pop_move(pos, ss); // illegal move
            continue;
        }



        // calculate late move reduction

        bool can_lmr = legal_move_index > 3 && depth > 3 && !in_check && quiet;
        int lmr = 0;

        if (can_lmr) {
            float frac = 0.5f + std::log(float(depth)) * std::log(float(legal_move_index)) / 4.0f;
            lmr = int(std::round(frac));
            lmr = std::max(lmr, 0);
        }




        // perform principal variation search

        int32_t score = 0;

        if (!pv_node || (legal_move_index > 0)) {
            score = -search(pos, s, depth-1-lmr, ply+1, -(alpha+1), -alpha, ss+1);

            if (lmr > 0 && score > alpha) {
                score = -search(pos, s, depth-1, ply+1, -(alpha+1), -alpha, ss+1);
            }
        }

        if (pv_node && (legal_move_index == 0 || score > alpha)) {
            score = -search(pos, s, depth-1, ply+1, -beta, -alpha, ss+1);
        }

        if (s.exited) {
            pop_move(pos, ss);
            return 0;
        }






        if (score > best_score) {
            best_score = score;
        }

        if (score > alpha) {
            alpha = score;
            best_move = mv;
        }

        if (alpha >= beta) {
            s.metrics->beta_cutoff_index_sum += legal_move_index;
            s.metrics->beta_cutoff_count++;

            pop_move(pos, ss);

            if (quiet) {
                int16_t history_bonus = int16_t(300 * depth - 250);
                s.history.update(side, move_from(mv), move_to(mv), history_bonus);

                for (int i = quiet_count-2; i >= 0; --i) {
                    s.history.update(side, move_from(quiets[i]), move_to(quiets[i]), int16_t(-history_bonus));
                }
            }

            s.tt_write(pos.zobrist, mv, int16_t(best_score), int8_t(depth), TT_LOWER, ply);

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

    s.tt_write(pos.zobrist, best_move, int16_t(best_score), int8_t(depth), best_score > alpha0 ? TT_EXACT : TT_UPPER, ply);

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
    
    for (int d = 1; d <= depth; ++d) {
        if (!s.budgeter->should_start_next_iteration()) {
            break;
        }

        Move mv;
        int32_t score = search(*this, s, d, 0, -INF_SCORE, INF_SCORE, ss, &mv);

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
        stats->pv_nodes = metrics.pv_node_count;

        stats->nmps = metrics.nmp_count;
        stats->rfps = metrics.rfp_count;

        stats->time = float(double(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count())/1000000.0);
        stats->mean_cutoff_index = float(metrics.beta_cutoff_index_sum)/float(metrics.beta_cutoff_count);
        stats->sel_depth = metrics.sel_depth;
        stats->tt_hit_rate = float(metrics.tt_matches)/float(metrics.tt_queries);
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