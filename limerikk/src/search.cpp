#include <array>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <unordered_set>

#include "limerikk.h"

constexpr int32_t BEST_MOVE_SCORE    = 2000000;
constexpr int32_t GOOD_CAPTURE_SCORE = 900000;
constexpr int32_t KILLER_1_SCORE     = 700000;
constexpr int32_t KILLER_2_SCORE     = 600000;
constexpr int32_t MAX_HISTORY_SCORE  = 80000;
constexpr int32_t BAD_CAPTURE_SCORE  = -900000;

constexpr uint64_t TT_MASK = TRANSPOSITION_TABLE_SIZE - 1;

//using LMRTable = std::array<std::array<int, 64>,64>;

static int get_reduction(int d, int i, const SearchParameters& params) {
    float reduction = params.lmr_rate_base + std::log(float(d)) * std::log(float(i)) / params.lmr_rate_divisor;
    int r = int(reduction);
    if (r < 0) r = 0;
    return r;
}

//static LMRTable generate_lmr_table() {
//    LMRTable table;
//
//    for (int d = 1; d < 64; d++) {
//        for (int i = 1; i < 64; i++) {
//            table[d][i] = get_reduction(d, i, 1.35f);
//        }
//    }
//
//    return table;
//}
//
//static const LMRTable lmr_table = generate_lmr_table();

enum TTScoreFlag {
    TT_SCORE_EXACT,
    TT_SCORE_UPPER,
    TT_SCORE_LOWER
};

static Move select_best(MoveList& moves, std::span<int32_t>& move_scores, int index) {
    int32_t best_score = INT32_MIN;
    int best_index = -1;

    for (int i = index; i < moves.count; ++i) {
        int32_t score = move_scores[i];

        if (score > best_score) {
            best_score = score;
            best_index = i;
        }
    }

    assert(best_index != -1);

    std::swap(moves.data[index], moves.data[best_index]);
    std::swap(move_scores[index], move_scores[best_index]);

    return moves.data[index];
}

int32_t Position::mvv_lva_score(Move mv, int32_t offset) const{
    Piece captured_piece = move_captured_piece(mv);
    Piece moving_piece = (Piece)piece_at[move_from(mv)];
    int32_t score = offset + piece_value_table[captured_piece] * 1000 - piece_value_table[moving_piece];
    return captured_piece == PIECE_NONE ? 0 : score;
}

static uint32_t compress_zobrist(uint64_t zobrist) {
    return uint32_t(zobrist >> 32);
}

bool update_tt_entry(TTEntry& entry, uint64_t zobrist, int depth, int64_t score, int ply, int64_t alpha_original, int64_t beta_original, Move best_move) {
    if (entry.key32 != compress_zobrist(zobrist) || depth > entry.depth) {
        entry.key32 = compress_zobrist(zobrist);

        assert(depth <= UINT8_MAX);
        entry.depth = (uint8_t)depth;

        assert(int64_t(int16_t(score)) == score); // ensure the truncation preserves the score
        entry.score = int16_t(score);

        if (entry.score < -MATE_SCORE + 1000) {
            entry.score -= int16_t(ply);
        }
        else if (entry.score > MATE_SCORE - 1000) {
            entry.score += int16_t(ply); // remove the current ply so that the mate score is relative to here rather than the root
        }

        if (score <= alpha_original) {
            entry.flag = TT_SCORE_UPPER; // 
        }
        else if (score >= beta_original) {
            entry.flag = TT_SCORE_LOWER; // true score is AT LEAST best_score
        }
        else {
            entry.flag = TT_SCORE_EXACT; // best_score IS the true score
        }

        entry.best_move = best_move;

        return true;
    }

    return false;
}

static std::span<int32_t> compute_move_scores(Position* pos, HistoryTable& history, KillerTable& killers, int ply, std::span<int32_t> score_buf, const MoveList& moves, Move best_move, ContinuationTable* cont) {
    std::span<int32_t> move_scores = score_buf.subspan(0, moves.count);

    for (int i = 0; i < moves.count; ++i) {
        Move mv = moves.data[i];

        bool quiet = !is_capture(mv);

        if (mv == best_move) {
            move_scores[i] = BEST_MOVE_SCORE;
        }
        else if (mv == killers[ply][0]) {
            move_scores[i] = KILLER_1_SCORE;
        }
        else if (mv == killers[ply][1]) {
            move_scores[i] = KILLER_2_SCORE;
        }
        else if (!quiet) {
            int offset = pos->see(mv) < 0 ? BAD_CAPTURE_SCORE : GOOD_CAPTURE_SCORE;
            move_scores[i] = pos->mvv_lva_score(mv, offset);
        }
        else {
            Piece piece = (Piece)pos->piece_at[move_from(mv)];
            int to = move_to(mv);

            int score = history[piece][to];

            if (cont) {
                score += int(cont->at(piece)[to]);
            }

            move_scores[i] = score;
        }
    }

    return move_scores;
}

#define PREFETCH_TT() PREFETCH(&s.tt[zobrist & TT_MASK])

static TTEntry& find_entry(TranspositionTable& tt, uint64_t zobrist) {
    TTCluster& cluster = tt[zobrist & TT_MASK];

    // find match
    for (auto& e : cluster.entries) {
        if (e.key32 == compress_zobrist(zobrist)) {
            return e;
        }
    }

    // find empty
    for (auto& e : cluster.entries) {
        if (e.key32 == 0) {
            return e;
        }
    }

    size_t shallowest = 0;

    for (size_t i = 1; i < std::size(cluster.entries); ++i) {
        if (cluster.entries[i].depth < cluster.entries[shallowest].depth) {
            shallowest = i;
        }
    }
    
    return cluster.entries[shallowest];
}

int64_t Position::negamax(SearchContext& s, int depth, int ply, bool allow_null, int64_t alpha, int64_t beta, Move excluded_move, int extensions_so_far, int root_depth, ContinuationTable* cont) {
    if ((node_count & 4095) == 0) {
        if (s.budgeter->should_exit(*this)) {
            s.should_stop = true;
        }
    }

    if (s.should_stop) {
        return 0;
    }

    node_count++;

    if (is_threefold_repetition()) {
        return 0;
    }

    if (depth == 0) {
        return quiescence(s, ply, alpha, beta);
    }

    max_ply = std::max(max_ply, ply);

    bool is_pv = (beta-alpha) > 1;
    pv_node_count += is_pv;

    int my_side = to_move;
    bool currently_checked = is_checked[my_side];

    bool improving = false;
    if (ply >= 2 && !currently_checked) {
        improving = signed_eval() >= s.eval_history[ply-2];
    }

    s.eval_history[ply] = signed_eval();

    // First check TT

    int64_t alpha_original = alpha; // store this for when we update the transposition table
    int64_t beta_original  = beta; // store this for when we update the transposition table

    Move tt_move = NULL_MOVE;

    TTEntry& match = find_entry(s.tt, zobrist);

    if (match.key32 == compress_zobrist(zobrist)) { // exact match
        if (excluded_move == NULL_MOVE && match.depth >= depth) {
            int64_t entry_score = int64_t(match.score);

            if (entry_score < -MATE_SCORE + 1000) {
                entry_score += ply;
            }
            else if (entry_score >  MATE_SCORE - 1000) {
                entry_score -= ply; // reapply the ply to mate scores to restore them to relative to the root
            }

            switch (match.flag) {
                case TT_SCORE_EXACT:
                    return entry_score; // exact store stored, we can just return it

                case TT_SCORE_LOWER:
                    alpha = std::max(alpha, entry_score);
                    break;

                case TT_SCORE_UPPER:
                    beta = std::min(beta, entry_score);
                    break;
            }

            if (alpha >= beta) {
                beta_cutoffs++;
                return entry_score;
            }
        }

        tt_move = match.best_move;
    }

    // Singular extensions
    // we do a narrow search while "banning" the TT move, which we expect to be the best move
    // if that search fails miserably, we can be confident that the TT move is essentially forced
    // then we can extend the depth of the TT search by 1
    // since we are so confident in this line, we can crank up LMR (which is essentially every other move)

    bool tt_is_singular = false;
    if (depth >= 6 &&
        tt_move != NULL_MOVE &&
        excluded_move == NULL_MOVE &&
        match.flag != TT_SCORE_UPPER &&
        (std::abs(match.score) < (MATE_SCORE - 1000)) &&
        match.depth >= depth - 3) {
        int singular_margin = int(std::round(s.params.singular_margin_factor * float(depth)));
        int singular_beta = match.score - singular_margin;

        int64_t exc_score = negamax(s, depth/2, ply, false, singular_beta-1, singular_beta, tt_move, extensions_so_far, root_depth, nullptr);

        if (exc_score < singular_beta) {
            tt_is_singular = true; // no other move can even come close 
        }
        else if (singular_beta >= beta) { // another move beats beta, we might be able to fail-high immediately
            return exc_score; 
        }
    }

    // we didn't find a TT move; branch probably boring
    if (tt_move == NULL_MOVE && depth >= 4) {
        depth--;
    }

    int64_t best_score = -MATE_SCORE;

    // Reverse futility pruning - 

    if (depth <= 3 && !currently_checked && std::abs(beta) < MATE_SCORE - 1000) { // we disable this if we are mating or we are being mated
        int64_t margin = s.params.rfp_margin_factor * depth;

        if (!improving) {
            margin += s.params.rfp_improving_bonus;
        }
        else {
            margin -= s.params.rfp_improving_bonus;
        }

        margin = std::max(int64_t(0), margin);

        int64_t ev = signed_eval();

        if (ev - margin >= beta) {
            beta_cutoffs++;
            return ev - margin; 
        }
    }

    // Null move pruning
    // if we give the opponent a free move and alpha >= beta, our position is too good, so prune

    bool low_material = non_pawn_value(my_side) <= 2 * piece_value_table[PIECE_KNIGHT];
    bool skip_null = !allow_null || currently_checked || low_material || alpha >= MATE_SCORE - 1000;

    int R = int(std::round(s.params.nmp_r_base + float(depth) / s.params.nmp_r_divisor)); // we subtract this from depth to reduce the search depth

    if (depth > R + 1 && !skip_null) { 
        make_null_move(); 

        int64_t null_alpha = beta - 1; // we only care if it can beat it, not the actual score
        int64_t null_beta = beta;
        
        int64_t score = -negamax(s, depth - 1 - R, ply + 1, false, -null_beta, -null_alpha, NULL_MOVE, extensions_so_far, root_depth, nullptr);

        unmake_null_move();

        if (score >= beta) {
            TTEntry& target = find_entry(s.tt, zobrist);
            bool changed = update_tt_entry(target, zobrist, depth, beta, ply, alpha_original, beta_original, NULL_MOVE);
            if (changed) { target.flag = TT_SCORE_LOWER; }
            beta_cutoffs++;
            null_prunes++;
            return beta;
        }
    }

    MoveList moves = generate_moves();

    std::array<int32_t, 256> score_buf;
    std::span<int32_t> move_scores = compute_move_scores(this, s.history, s.killers, ply, score_buf, moves, tt_move, cont);

    bool futility_prune = false;
    if (depth <= 3 && !currently_checked && (std::abs(alpha) < MATE_SCORE - 1000)) {
        int f_margin = depth * s.params.fp_margin_factor;
        if (signed_eval() + f_margin <= alpha) {
            futility_prune = true;
        }
    }

    Move best_move = NULL_MOVE;

    int move_index = 0; // the index of move out of all LEGAL moves

    std::array<Move, 256> searched_quiets;
    int searched_quiet_count = 0;

    for (int mv_idx_raw = 0; mv_idx_raw < moves.count; ++mv_idx_raw) {
        Move m = select_best(moves, move_scores, mv_idx_raw);

        if (m == excluded_move) {
            continue;
        }

        Piece piece = (Piece)piece_at[move_from(m)];
        int to = move_to(m);

        bool cutoff = false;
        bool quiet = !is_capture(m);

        ContinuationTable* next_cont = &s.cont_history[piece][to];

        make_move(m);

        if (!is_checked[my_side]) {
            PREFETCH_TT();

            bool gives_check = is_checked[opponent(my_side)];

            // Late move reduction

            int reduction = 0;
            bool bad_capture = !quiet && (see(m) < 0);

            //bool is_killer = m == s.killers[ply][0] || m == s.killers[ply][1];

            if (depth >= 2 && (quiet || bad_capture) && !currently_checked && move_index >= 3 && !gives_check/* && !is_killer*/) {
                int idx = std::min(move_index, 63);
                reduction = get_reduction(depth, idx, s.params);

                if (depth <= 2) {
                    reduction = std::max(0, reduction - 1);
                }

                if (s.history[piece][to] > s.params.lmr_history_bonus_threshold) {
                    reduction = std::max(0, reduction - 1);
                }
                else
                if (s.history[piece][to] < 0) {
                    reduction++; // this move has historically been ass -> reduce ts
                }

                if (!is_pv) {
                    reduction++; // this move is PROBABLY ass anyway, so slash the search depth
                }

                if (improving) {
                    reduction = std::max(0, reduction - 1);
                }
                else {
                    reduction++;
                }

                reduction = std::min(reduction, std::max(0, depth - 2));
            }

            if (futility_prune && quiet && !gives_check) {
                unmake_move();
                continue;
            }

            // Late move pruning

            if (depth <= 4 && !currently_checked && quiet && !gives_check) {
                int move_threshold = int(std::round(s.params.lmp_index_base + s.params.lmp_index_factor * float(depth * depth)));
                if (move_index > move_threshold) { 
                    unmake_move();
                    continue; 
                }
            }

            int64_t score;

            int check_ext = gives_check && (extensions_so_far < root_depth);

            if (move_index == 0) {
                int ext = check_ext;

                if (m == tt_move && tt_is_singular && (extensions_so_far < root_depth)) {
                    ext = std::max(ext, 1);
                }

                score = -negamax(s, depth - 1 + ext, ply + 1, true, -beta, -alpha, NULL_MOVE, extensions_so_far + ext, root_depth, next_cont);
            }
            else {
                reduced_searches += reduction > 0;

                // don't extend the null-window search
                score = -negamax(s, depth - 1 - reduction, ply + 1, true, -alpha-1, -alpha, NULL_MOVE, extensions_so_far, root_depth, next_cont); // do null-window search

                if (score > alpha) { // if beats alpha do full-window
                    reduced_fail_high += reduction > 0;
                    // DO extend the research
                    score = -negamax(s, depth - 1 + check_ext, ply + 1, true, -beta, -alpha, NULL_MOVE, extensions_so_far + check_ext, root_depth, next_cont);
                }
            }

            if (score > best_score) {
                best_score = score;
                best_move = m;
            }

            if (score > alpha) {
                alpha = score;
            }

            if (alpha >= beta) { // opponent will never allow this; cutoff
                cutoff = true;
            }

            move_index++;
        }

        unmake_move();

        if (cutoff) {
            if (quiet) {
                if (m != s.killers[ply][0]) {
                    s.killers[ply][1] = s.killers[ply][0];
                    s.killers[ply][0] = m;
                }

                {
                    auto hist = &s.history[piece][to];
                    s.history[piece][to] += int(std::round(s.params.history_bonus_factor * float(depth * depth)));
                    *hist = std::clamp(*hist, -MAX_HISTORY_SCORE, MAX_HISTORY_SCORE);
                }

                if (cont) {
                    auto c = &cont->at(piece)[to];
                    *c += int32_t(std::round(s.params.cont_history_bonus_factor * float(depth * depth)));
                    *c = std::clamp(*c, -MAX_HISTORY_SCORE, MAX_HISTORY_SCORE);
                }

                for (int i = 0; i < searched_quiet_count; ++i) { // we have a quiet cutoff, penalize all previous cutoffs in the history table
                    Move punished = searched_quiets[i];

                    Piece punished_piece = Piece(piece_at[move_from(punished)]);
                    int punished_to = move_to(punished);
                    
                    auto hist = &s.history[punished_piece][punished_to];
                    *hist -= int(std::round(s.params.history_malus_factor * float(depth * depth)));
                    *hist = std::clamp(*hist, -MAX_HISTORY_SCORE, MAX_HISTORY_SCORE);

                    if (cont) {
                        auto c = &cont->at(punished_piece)[punished_to];
                        *c -= int(std::round(s.params.cont_history_malus_factor * float(depth*depth)));
                        *c = std::clamp(*c, -MAX_HISTORY_SCORE, MAX_HISTORY_SCORE);
                    }
                }
            }

            cutoff_index_count++;
            cutoff_index_sum += (move_index-1);
            beta_cutoffs++;

            break;
        } 
        else {
            if (quiet) {
                searched_quiets[searched_quiet_count++] = m;
            }
        }
    }

    if (move_index == 0) { // no legal moves
        if (is_checked[my_side]) {
            best_score = -MATE_SCORE + ply; // checkmate
        }
        else {
            best_score = 0; // stalemate
        }
    }

    if (half_move_clock == 100) {
        return 0; // we don't want to store this in the TT
    }

    TTEntry& target = find_entry(s.tt, zobrist);
    update_tt_entry(target, zobrist, depth, best_score, ply, alpha_original, beta_original, best_move);

    return best_score;
}

int64_t Position::quiescence(SearchContext& s, int ply, int64_t alpha, int64_t beta) {
    if ((node_count & 4095) == 0) {
        if (s.budgeter->should_exit(*this)) {
            s.should_stop = true;
        }
    }

    if (s.should_stop) {
        return 0;
    }

    node_count++;
    qnode_count++;

    if (is_threefold_repetition()) {
        return 0;
    }

    int side = to_move;
    bool currently_checked = is_checked[side];

    int64_t best_score = -MATE_SCORE;
    int64_t stand_pat = signed_eval();

    uint64_t promotion_rank = side == WHITE ? RANK_7 : RANK_2;
    uint64_t pawns = sides[side].bb[PIECE_PAWN];
    
    bool can_delta_prune = (pawns & promotion_rank) == 0;

    if (!currently_checked) { // if not checked, we can choose to stay here
        best_score = stand_pat;

        alpha = std::max(stand_pat, alpha);

        if (alpha >= beta) {
            beta_cutoffs++;
            return stand_pat;
        }

        if (can_delta_prune && (stand_pat < alpha - s.params.qsearch_big_delta)) { // even a queen capture can't save us
            return stand_pat;
        }
    }

    MoveList moves = currently_checked ? generate_moves() : generate_captures();

    std::array<int32_t, 256> score_buf;
    std::span<int32_t> move_scores = std::span(score_buf).subspan(0, moves.count);

    for (int i = 0; i < moves.count; ++i) {
        Move mv = moves.data[i];
        move_scores[i] = mvv_lva_score(mv, 0);
    }

    bool legal_found = false;

    for (int i = 0; i < moves.count; ++i) {
        Move mv = select_best(moves, move_scores, i);

        bool quiet = !is_capture(mv);

        if (!quiet && !currently_checked) {
            if (can_delta_prune)
            {
                int32_t capture_value = piece_value_table[move_captured_piece(mv)];

                if (stand_pat + capture_value + s.params.qsearch_delta_margin < alpha) {
                    continue;
                }
            }

            if (see(mv) < 0) {
                continue;
            }
        }

        bool cutoff = false;

        make_move(mv);

        if (!is_checked[side]) {
            legal_found = true;

            int64_t score = -quiescence(s, ply+1, -beta, -alpha);

            if (score > best_score) {
                best_score = score;
            }

            if (score > alpha) {
                alpha = score;
            }

            if (alpha >= beta) {
                cutoff = true;
            }
        }

        unmake_move();

        if (cutoff) {
            beta_cutoffs++;
            return best_score;
        }
    }

    if (currently_checked && !legal_found) {
        return -MATE_SCORE + ply; // checkmate
    }

    if (half_move_clock == 100) {
        return 0;
    }

    return best_score;
}

std::pair<Move, int64_t> Position::best_move_internal(SearchContext& s, MoveList& moves, int depth, Move last_best_move, int64_t alpha, int64_t beta) {
    (void)last_best_move;

    int ply = 1;

    int64_t best_score = -INF;
    Move best_move = NULL_MOVE;

    std::array<int32_t, 256> score_buf;
    std::span<int32_t> move_scores = compute_move_scores(this, s.history, s.killers, ply, score_buf, moves, last_best_move, nullptr);

    for (int i = 0; i < moves.count; ++i) {
        Move m = select_best(moves, move_scores, i);

        Piece piece = Piece(piece_at[move_from(m)]);
        int to = move_to(m);

        ContinuationTable* cont = &s.cont_history[piece][to];

        make_move(m); // no need to filter for check here - assumes filtered moves given
        PREFETCH_TT();
        int64_t score = -negamax(s, depth-1, ply+1, true, -beta, -alpha, NULL_MOVE, 0, depth-1, cont);
        unmake_move();

        if (score > best_score) {
            best_score = score;
            best_move = m;
        }

        alpha = std::max(alpha, score);

        if (alpha >= beta) {
            beta_cutoffs++;
            break;
        }
    }

    return {best_move, best_score};
}

Move Position::best_move(int depth, std::atomic<bool>& should_stop, Budgeter* budgeter, const SearchParameters& params_in, bool enable_uci_info, int64_t* score_out) {
    reset_benchmarking_statistics();

    MoveList moves = generate_moves();
    filter_moves(moves);

    if (moves.count == 0) {
        return NULL_MOVE;
    }

    auto s = std::make_unique<SearchContext>(params_in, should_stop, budgeter);

    TimePoint start_time = Clock::now();
    budgeter->init();

    Move best_move = moves.data[0]; // have at least one move
    int64_t best_score = 0;

    for (int i = 1; i <= depth; ++i) {
        int64_t window = s->params.asp_initial_window_size; // start the window small

        int64_t alpha = best_score - window;
        int64_t beta  = best_score + window;

        while (true) {
            auto [move, score] = best_move_internal(*s.get(), moves, i, best_move, alpha, beta);

            if (s->should_stop) {
                break;
            }

            if (score <= alpha) {
                // fail low
                alpha -= window;
                window = int64_t(float(window) * s->params.asp_window_growth_factor);
            }
            else if (score >= beta) {
                // fail high
                best_move = move;
                beta += window;
                window = int64_t(float(window) * s->params.asp_window_growth_factor);
            }
            else {
                best_move = move;
                best_score = score;
                break;
            }
        }

        if (s->should_stop) {
            break;
        }

        // UCI output

        if (enable_uci_info) {
            double elapsed = double(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_time).count())/1000.0;
            int nps = int(double(node_count)/elapsed);

            std::string score_str;
            if (std::abs(best_score) > MATE_SCORE - 1000) {
                int moves_to_mate = int((MATE_SCORE - std::abs(best_score) + 1) / 2);
                score_str = std::format("mate {}", best_score > 0 ? moves_to_mate : -moves_to_mate);
            } else {
                score_str = std::format("cp {}", best_score);
            }
            
            int64_t nnue_score = nnue_eval();

            if (to_move == BLACK) {
                nnue_score *= -1;
            }

            uint64_t initial_zobrist = zobrist;
            (void)initial_zobrist;

            // extract pv
            std::vector<Move> pv_list;
            pv_list.push_back(best_move);
            make_move(best_move);

            std::unordered_set<uint64_t> seen;
            seen.insert(zobrist);

            for (int i = 0; i < 50; ++i) {
                TTEntry& entry = find_entry(s->tt, zobrist);

                if (entry.key32 != compress_zobrist(zobrist)) {
                    break; // TT miss
                }

                if (entry.best_move == NULL_MOVE) {
                    break; // No TT move
                }

                if (!is_move_legal_slow(entry.best_move)) {
                    break;
                }

                pv_list.push_back(entry.best_move);
                make_move(entry.best_move);

                if (seen.count(zobrist)) { // cycle
                    break;
                }

                seen.insert(zobrist);
            }

            std::string pv_string;

            for (size_t i = 0; i < pv_list.size(); ++i) {
                unmake_move();
            }

            assert(zobrist == initial_zobrist);

            for (size_t i = 0; i < pv_list.size(); ++i) {
                if (i > 0) {
                    pv_string += " ";
                }

                pv_string += to_uci_move(pv_list[i]);
            }

            std::cout << std::format("info depth {} seldepth {} score {} nnuescore {} nodes {} nps {} time {} pv {}\n", i, max_ply, score_str, nnue_score, node_count, nps, int(elapsed*1000.0), pv_string);
        }
    }

    if (score_out) {
        *score_out = best_score;
    }

    return best_move;
}

/**
    Chooses between an opening move and searching for best move
 */
Move Position::think(int depth, std::atomic<bool>& should_stop, Budgeter* budgeter, const SearchParameters& params_in, bool enable_uci_info) {
    uint64_t hash = encode_polyglot();
    std::vector<PolyglotEntry> p_moves = probe_book(hash);

    if (!p_moves.empty()) {
        PolyglotEntry line = choose_move(p_moves);
        Move move = decode_polyglot(line);
        assert(is_move_legal_slow(move));
        return move;
    } else {

        Move best = best_move(depth, should_stop, budgeter, params_in, enable_uci_info);
        assert(best != NULL_MOVE);
    
        return best;
    }
}