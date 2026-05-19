#include <array>
#include <bit>

#include "limerikk.h"
#include "generated_tables.h"

static constexpr std::array<std::array<uint32_t, 64>, 2> gen_rook_castle_flag_table() {
    std::array<std::array<uint32_t, 64>, 2> table{};

    table[WHITE][0] = POSITION_FLAG_WHITE_QCASTLE;
    table[WHITE][7] = POSITION_FLAG_WHITE_KCASTLE;

    table[BLACK][56] = POSITION_FLAG_BLACK_QCASTLE;
    table[BLACK][63] = POSITION_FLAG_BLACK_KCASTLE;

    return table;
}

static constexpr std::array<int, 64> gen_rook_jump_from_table() {
    std::array<int, 64> table{};

    table[6]  = 7;
    table[2]  = 0;
    table[62] = 63;
    table[58] = 56;

    return table;
}

static std::array<int, 64> gen_rook_jump_to_table() {
    std::array<int, 64> table{};

    table[6]  = 5;
    table[2]  = 3;
    table[62] = 61;
    table[58] = 59;

    return table;
}

static const std::array<std::array<uint32_t, 64>, 2> rook_castle_flag_table = gen_rook_castle_flag_table();
static const std::array<int, 64> rook_jump_from = gen_rook_jump_from_table();
static const std::array<int, 64> rook_jump_to = gen_rook_jump_to_table();

// King
uint64_t king_moves(int from, uint64_t allies) {
    return king_move_table[from] & (~allies);
}

// Knights
uint64_t knight_moves(int from, uint64_t allies) {
    return knight_move_table[from] & (~allies);
}

// Pawns
uint64_t white_pawn_single_push(uint64_t bb, uint64_t all_pieces) {
    uint64_t push  = (bb << 8) & (~all_pieces) & (~RANK_1);
    return push;
}

uint64_t white_pawn_double_push(uint64_t bb, uint64_t all_pieces) {
    bb &= RANK_2;

    uint64_t push  = (bb << 8) & (~all_pieces);
    uint64_t double_push = (push << 8) & (~all_pieces);

    return double_push;
}

uint64_t white_pawn_left_capture_no_mask(uint64_t bb) {
    uint64_t left  = (bb << 7) & (~FILE_H);
    return left;
}

uint64_t white_pawn_right_capture_no_mask(uint64_t bb) {
    uint64_t right = (bb << 9) & (~FILE_A);
    return right;
}

uint64_t black_pawn_single_push(uint64_t bb, uint64_t all_pieces) {
    uint64_t push  = (bb >> 8) & (~all_pieces) & (~RANK_8);
    return push;
}

uint64_t black_pawn_left_capture_no_mask(uint64_t bb) {
    uint64_t left  = (bb >> 9) & (~FILE_H);
    return left;
}

uint64_t black_pawn_right_capture_no_mask(uint64_t bb) {
    uint64_t right = (bb >> 7) & (~FILE_A);
    return right;
}

uint64_t black_pawn_double_push(uint64_t bb, uint64_t all_pieces) {
    bb &= RANK_7;
    uint64_t push  = (bb >> 8) & (~all_pieces);
    uint64_t double_push = (push >> 8) & (~all_pieces);
    return double_push;
}

// Magic Bitboards
static size_t magic_index(uint64_t all_pieces, uint64_t mask, uint64_t magic, size_t shift) {
    return static_cast<size_t>(((all_pieces & mask) * magic) >> shift);
}

uint64_t rook_moves(int from, uint64_t all_pieces, uint64_t allies) {
    size_t index = magic_index(all_pieces, rook_mask[from], rook_magic[from], rook_shift[from]);
    uint64_t moves = rook_move[from][index];
    return moves & (~allies);
}

uint64_t bishop_moves(int from, uint64_t all_pieces, uint64_t allies) {
    size_t index = magic_index(all_pieces, bishop_mask[from], bishop_magic[from], bishop_shift[from]);
    uint64_t moves = bishop_move[from][index];
    return moves & (~allies);
}

uint64_t queen_moves(int from, uint64_t all_pieces, uint64_t allies) {
    return bishop_moves(from, all_pieces, allies) | rook_moves(from, all_pieces, allies);
}

bool Position::is_king_square_attacked(int side, int square) const {
    int opp = opponent(side);

    uint64_t all = all_pieces() & ~(sides[side].bb[PIECE_KING]);

    uint64_t pawn_mask = side == WHITE ? white_pawn_attacks_table[square] : black_pawn_attacks_table[square];

    if (pawn_mask & sides[opp].bb[PIECE_PAWN]) {
        return true;
    }

    uint64_t king_mask = king_moves(square, 0);

    if (king_mask & sides[opp].bb[PIECE_KING]) {
        return true;
    }

    uint64_t knight_mask = knight_moves(square, 0);

    if (knight_mask & sides[opp].bb[PIECE_KNIGHT]) {
        return true;
    }

    uint64_t diag_mask = bishop_moves(square, all, 0);
    uint64_t queens  = sides[opp].bb[PIECE_QUEEN];
    uint64_t bishops = sides[opp].bb[PIECE_BISHOP]; 

    if (diag_mask & (bishops | queens)) {
        return true;
    }

    uint64_t straight_mask = rook_moves(square, all, 0);
    uint64_t rooks = sides[opp].bb[PIECE_ROOK];

    if (straight_mask & (rooks | queens)) {
        return true;
    }

    return false;
}

int Position::lowest_value_defender(int defender_side, int square, uint64_t occupancy) const {
    uint64_t pawn_mask     = occupancy & ((defender_side == BLACK) ? white_pawn_attacks_table[square] : black_pawn_attacks_table[square]);
    uint64_t pawn_defenders = pawn_mask & sides[defender_side].bb[PIECE_PAWN];

    if (pawn_defenders != 0) {
        return std::countr_zero(pawn_defenders);
    }

    uint64_t knight_mask   = occupancy & knight_moves(square, 0);
    uint64_t knight_defenders = knight_mask & sides[defender_side].bb[PIECE_KNIGHT];

    if (knight_defenders != 0) {
        return std::countr_zero(knight_defenders);
    }

    uint64_t diag_mask     = occupancy & bishop_moves(square, occupancy, 0);
    uint64_t bishop_defenders = diag_mask & sides[defender_side].bb[PIECE_BISHOP];

    if (bishop_defenders != 0) {
        return std::countr_zero(bishop_defenders);
    }

    uint64_t straight_mask = occupancy & rook_moves(square, occupancy, 0);
    uint64_t rook_defenders = straight_mask & sides[defender_side].bb[PIECE_ROOK];

    if (rook_defenders != 0) {
        return std::countr_zero(rook_defenders);
    }

    uint64_t queen_defenders = (straight_mask | diag_mask) & sides[defender_side].bb[PIECE_QUEEN];

    if (queen_defenders != 0) {
        return std::countr_zero(queen_defenders);
    }

    uint64_t king_mask     = occupancy & king_moves(square, 0);
    uint64_t king_defenders = king_mask & sides[defender_side].bb[PIECE_KING];

    if (king_defenders != 0) {
        return std::countr_zero(king_defenders);
    }

    return NULL_SQUARE;
}

int Position::see(Move m) const {
    int value[32];
    int depth = 0;

    Piece attacker = (Piece)piece_at[move_from(m)];
    int sq = move_to(m); // The destination square - not the captured square

    Piece initial_captured = move_captured_piece(m);
    value[depth++] = piece_value_table[initial_captured]; // initial value of capture

    int defender_side = opponent(to_move);
    uint64_t occupancy = all_pieces();

    occupancy ^= sq_to_bb(move_from(m)); // remove the first attacker
    occupancy ^= sq_to_bb(move_captured_square(m)); // remove captured piece (en passant)
    
    for (;;) {
        int defender_sq = lowest_value_defender(defender_side, sq, occupancy);

        if (defender_sq == NULL_SQUARE) {
            break;
        }

        Piece defender = (Piece)piece_at[defender_sq];

        if (defender == PIECE_KING) {
            if (lowest_value_defender(opponent(defender_side), sq, occupancy ^ sq_to_bb(defender_sq)) != NULL_SQUARE) {
                break; // the king cannot defend because it will move into check
            }
        }

        int prev_value = value[depth-1];
        value[depth++] = piece_value_table[attacker] - prev_value; // the value of the PREVIOUS attacker

        attacker = defender;
        occupancy ^= sq_to_bb(defender_sq);
        defender_side = opponent(defender_side);
    }

    while (--depth > 0) {
        value[depth-1] = -std::max(-value[depth-1], value[depth]);
    }

    return value[0];
}

inline bool pinned(int sq, uint64_t pin_mask) {
    return (sq_to_bb(sq) & pin_mask) != 0;
}

static uint64_t restrictions(int sq, uint64_t pin_mask, int king_sq) {
    return pinned(sq, pin_mask) ? line[sq][king_sq] : UINT64_MAX;
}

static bool illegal_pin_move(int from, int to, int king_sq, uint64_t pin_mask) {
    return pinned(from, pin_mask) && ((sq_to_bb(to) & line[from][king_sq]) == 0);
}

#define new_move(from, to, type, end_piece) do { \
    int sq = get_captured_square(to, type, to_move); \
    moves.data[moves.count++] = encode_move(from, to, type, end_piece, to_move, (Piece)piece_at[sq]); \
} while (false)

MoveList Position::generate_moves() const {
    MoveList moves;
    moves.count = 0;

    int opp = opponent(to_move);
    uint64_t opps = sides[opp].all();
    uint64_t allies = sides[to_move].all();
    uint64_t all = all_pieces();

    int king_sq = get_king_sq(to_move);
    uint64_t pin_mask = generate_pin_mask(to_move);

    for (uint8_t from : set_bits(sides[to_move].bb[PIECE_KING])) {
        for (uint8_t to : set_bits(king_moves(from, allies))) {
            new_move(from, to, MOVE_NORMAL, PIECE_KING);
        }
    }

    // Castling

    uint32_t kcastle_flag = to_move == WHITE ? POSITION_FLAG_WHITE_KCASTLE : POSITION_FLAG_BLACK_KCASTLE;
    uint32_t qcastle_flag = to_move == WHITE ? POSITION_FLAG_WHITE_QCASTLE : POSITION_FLAG_BLACK_QCASTLE;

    uint64_t short_castle_space = to_move == WHITE ? WHITE_SHORT_SPACING : BLACK_SHORT_SPACING;
    uint64_t long_castle_space = to_move == WHITE ? WHITE_LONG_SPACING : BLACK_LONG_SPACING;

    bool can_kcastle = (flags & kcastle_flag) != 0 && ((short_castle_space & all) == 0);
    bool can_qcastle = (flags & qcastle_flag) != 0 && ((long_castle_space & all) == 0);

    if (is_checked[to_move]) {
        can_kcastle = false;
        can_qcastle = false;
    }

    if (can_kcastle) {
        can_kcastle = !is_king_square_attacked(to_move, to_move == WHITE ? 5 : 61)
                   && !is_king_square_attacked(to_move, to_move == WHITE ? 6 : 62);
    }

    if (can_qcastle) {
        can_qcastle = !is_king_square_attacked(to_move, to_move == WHITE ? 3 : 59)
                   && !is_king_square_attacked(to_move, to_move == WHITE ? 2 : 58);
    }

    if (can_kcastle) {
        uint8_t from = to_move == WHITE ? 4 : 60;
        uint8_t to   = to_move == WHITE ? 6 : 62;
        assert(std::countr_zero(sides[to_move].bb[PIECE_KING]) == from);
        assert(sides[to_move].bb[PIECE_ROOK] & sq_to_bb(to_move == WHITE ? 7 : 63));
        new_move(from, to, MOVE_SHORT_CASTLE, PIECE_KING);
    }

    if (can_qcastle) {
        uint8_t from = to_move == WHITE ? 4 : 60;
        uint8_t to   = to_move == WHITE ? 2 : 58;
        assert(std::countr_zero(sides[to_move].bb[PIECE_KING]) == from);
        assert(sides[to_move].bb[PIECE_ROOK] & sq_to_bb(to_move == WHITE ? 0 : 56));
        new_move(from, to, MOVE_LONG_CASTLE, PIECE_KING);
    }

    uint64_t knights = sides[to_move].bb[PIECE_KNIGHT] & (~pin_mask);

    for (uint8_t from : set_bits(knights)) {
        for (uint8_t to : set_bits(knight_moves(from, allies))) {
            new_move(from, to, MOVE_NORMAL, PIECE_KNIGHT);
        }
    }

    for (uint8_t from: set_bits(sides[to_move].bb[PIECE_ROOK])) {
        uint64_t bb = rook_moves(from, all, allies) & restrictions(from, pin_mask, king_sq);

        for (uint8_t to : set_bits(bb)) {
            new_move(from, to, MOVE_NORMAL, PIECE_ROOK);
        }
    }

    for (uint8_t from: set_bits(sides[to_move].bb[PIECE_BISHOP])) {
        uint64_t bb = bishop_moves(from, all, allies) & restrictions(from, pin_mask, king_sq);

        for (uint8_t to : set_bits(bb)) {
            new_move(from, to, MOVE_NORMAL, PIECE_BISHOP);
        }
    }

    for (uint8_t from: set_bits(sides[to_move].bb[PIECE_QUEEN])) {
        uint64_t bb = queen_moves(from, all, allies) & restrictions(from, pin_mask, king_sq);

        for (uint8_t to : set_bits(bb)) {
            new_move(from, to, MOVE_NORMAL, PIECE_QUEEN);
        }
    }

    auto pawn_single_push           = to_move == WHITE ? white_pawn_single_push : black_pawn_single_push;
    auto pawn_double_push           = to_move == WHITE ? white_pawn_double_push : black_pawn_double_push;
    auto pawn_left_capture_no_mask  = to_move == WHITE ? white_pawn_left_capture_no_mask : black_pawn_left_capture_no_mask;
    auto pawn_right_capture_no_mask = to_move == WHITE ? white_pawn_right_capture_no_mask : black_pawn_right_capture_no_mask;

    uint64_t ep_mask                = en_passant_sq == NULL_SQUARE ? 0 : sq_to_bb(en_passant_sq);
    uint64_t promotion_rank         = to_move == WHITE ? RANK_8 : RANK_1;

    auto new_pawn_single_move = [&](int from, int to) {
        if (sq_to_bb(to) & promotion_rank) {
            new_move(from, to, MOVE_PROMOTION, PIECE_QUEEN);
            new_move(from, to, MOVE_PROMOTION, PIECE_KNIGHT);
            new_move(from, to, MOVE_PROMOTION, PIECE_BISHOP);
            new_move(from, to, MOVE_PROMOTION, PIECE_ROOK);
        } 
        else {
            new_move(from, to, MOVE_NORMAL, PIECE_PAWN);
        }
    };

    for (int to : set_bits(pawn_single_push(sides[to_move].bb[PIECE_PAWN], all))) {
        int offset[] = { -8, 8 };
        int from = to + offset[to_move];

        if (!illegal_pin_move(from, to, king_sq, pin_mask)) {
            new_pawn_single_move(from, to);
        }
    }

    for (int to : set_bits(pawn_double_push(sides[to_move].bb[PIECE_PAWN], all))) {
        int offset[] = { -16, 16 };
        int from = to + offset[to_move];

        if (!illegal_pin_move(from, to, king_sq, pin_mask)) {
            new_move(from, to, MOVE_DOUBLE_PUSH, PIECE_PAWN);
        }
    }

    for (int to : set_bits(pawn_left_capture_no_mask(sides[to_move].bb[PIECE_PAWN]) & opps)) {
        int offset[] = { -7, 9 };
        int from = to + offset[to_move];

        if (!illegal_pin_move(from, to, king_sq, pin_mask)) {
            new_pawn_single_move(from, to);
        }
    }

    for (int to : set_bits(pawn_right_capture_no_mask(sides[to_move].bb[PIECE_PAWN]) & opps)) {
        int offset[] = { -9, 7 };
        int from = to + offset[to_move];

        if (!illegal_pin_move(from, to, king_sq, pin_mask)) {
            new_pawn_single_move(from, to);
        }
    }

    for (int to : set_bits(pawn_left_capture_no_mask(sides[to_move].bb[PIECE_PAWN]) & ep_mask)) {
        int offset[] = { -7, 9 };
        int from = to + offset[to_move];

        if (!illegal_pin_move(from, to, king_sq, pin_mask)) {
            new_move(from, to, MOVE_EN_PASSANT, PIECE_PAWN);
        }
    }

    for (int to : set_bits(pawn_right_capture_no_mask(sides[to_move].bb[PIECE_PAWN]) & ep_mask)) {
        int offset[] = { -9, 7 };
        int from = to + offset[to_move];

        if (!illegal_pin_move(from, to, king_sq, pin_mask)) {
            new_move(from, to, MOVE_EN_PASSANT, PIECE_PAWN);
        }
    }

    return moves;
}

MoveList Position::generate_captures() const {
    MoveList moves;
    moves.count = 0;

    int opp = opponent(to_move);
    uint64_t opps = sides[opp].all();
    uint64_t allies = sides[to_move].all();
    uint64_t all = all_pieces();

    int king_sq = get_king_sq(to_move);
    uint64_t pin_mask = generate_pin_mask(to_move);

    for (uint8_t from : set_bits(sides[to_move].bb[PIECE_KING])) {
        for (uint8_t to : set_bits(king_moves(from, allies) & opps)) {
            new_move(from, to, MOVE_NORMAL, PIECE_KING);
        }
    }

    for (uint8_t from : set_bits(sides[to_move].bb[PIECE_KNIGHT] & ~(pin_mask))) {
        for (uint8_t to : set_bits(knight_moves(from, allies) & opps)) {
            new_move(from, to, MOVE_NORMAL, PIECE_KNIGHT);
        }
    }

    for (uint8_t from: set_bits(sides[to_move].bb[PIECE_ROOK])) {
        uint64_t bb = rook_moves(from, all, allies) & opps & restrictions(from, pin_mask, king_sq);
        for (uint8_t to : set_bits(bb)) {
            new_move(from, to, MOVE_NORMAL, PIECE_ROOK);
        }
    }

    for (uint8_t from: set_bits(sides[to_move].bb[PIECE_BISHOP])) {
        uint64_t bb = bishop_moves(from, all, allies) & opps & restrictions(from, pin_mask, king_sq);
        for (uint8_t to : set_bits(bb)) {
            new_move(from, to, MOVE_NORMAL, PIECE_BISHOP);
        }
    }

    for (uint8_t from: set_bits(sides[to_move].bb[PIECE_QUEEN])) {
        uint64_t bb = queen_moves(from, all, allies) & opps & restrictions(from, pin_mask, king_sq);
        for (uint8_t to : set_bits(bb)) {
            new_move(from, to, MOVE_NORMAL, PIECE_QUEEN);
        }
    }

    auto pawn_left_capture_no_mask  = to_move == WHITE ? white_pawn_left_capture_no_mask : black_pawn_left_capture_no_mask;
    auto pawn_right_capture_no_mask = to_move == WHITE ? white_pawn_right_capture_no_mask : black_pawn_right_capture_no_mask;

    uint64_t ep_mask                = en_passant_sq == NULL_SQUARE ? 0 : sq_to_bb(en_passant_sq);
    uint64_t promotion_rank         = to_move == WHITE ? RANK_8 : RANK_1;

    auto new_pawn_single_move = [&](int from, int to) {
        if (sq_to_bb(to) & promotion_rank) {
            new_move(from, to, MOVE_PROMOTION, PIECE_QUEEN);
            new_move(from, to, MOVE_PROMOTION, PIECE_KNIGHT);
            new_move(from, to, MOVE_PROMOTION, PIECE_BISHOP);
            new_move(from, to, MOVE_PROMOTION, PIECE_ROOK);
        } 
        else {
            new_move(from, to, MOVE_NORMAL, PIECE_PAWN);
        }
    };

    for (int to : set_bits(pawn_left_capture_no_mask(sides[to_move].bb[PIECE_PAWN]) & opps)) {
        int offset[] = { -7, 9 };
        int from = to + offset[to_move];

        if (!illegal_pin_move(from, to, king_sq, pin_mask)) {
            new_pawn_single_move(from, to);
        }
    }

    for (int to : set_bits(pawn_right_capture_no_mask(sides[to_move].bb[PIECE_PAWN]) & opps)) {
        int offset[] = { -9, 7 };
        int from = to + offset[to_move];

        if (!illegal_pin_move(from, to, king_sq, pin_mask)) {
            new_pawn_single_move(from, to);
        }
    }

    for (int to : set_bits(pawn_left_capture_no_mask(sides[to_move].bb[PIECE_PAWN]) & ep_mask)) {
        int offset[] = { -7, 9 };
        int from = to + offset[to_move];

        if (!illegal_pin_move(from, to, king_sq, pin_mask)) {
            new_move(from, to, MOVE_EN_PASSANT, PIECE_PAWN);
        }
    }

    for (int to : set_bits(pawn_right_capture_no_mask(sides[to_move].bb[PIECE_PAWN]) & ep_mask)) {
        int offset[] = { -9, 7 };
        int from = to + offset[to_move];

        if (!illegal_pin_move(from, to, king_sq, pin_mask)) {
            new_move(from, to, MOVE_EN_PASSANT, PIECE_PAWN);
        }
    }

    return moves;
}

void Position::filter_moves(MoveList& moves) {
    int side = to_move;

    for (int i = moves.count-1; i >= 0; --i) {
        bool illegal = false;

        make_move(moves.data[i]);

        if (is_checked[side]) {
            illegal = true;
        }

        unmake_move();

        if (illegal) {
            moves.data[i] = moves.data[--moves.count];
        }
    }
}

std::unordered_map<std::string, Move> Position::name_moves(std::span<Move> moves_in) {
    std::vector<Move> at[64];

    for (Move mv : moves_in) {
        at[move_to(mv)].push_back(mv);
    }

    std::unordered_map<std::string, Move> names;

    for (auto& moves : at) {
        for (size_t i = 0; i < moves.size(); ++i) {
            Move m = moves[i];

            auto [f1, r1] = square_alg(move_from(m));

            bool is_capture = (piece_at[move_to(m)] != PIECE_NONE) || move_type(m) == MOVE_EN_PASSANT;
            bool need_file = piece_at[move_from(m)] == PIECE_PAWN && is_capture;
            bool need_rank = false;

            for (size_t j = 0; j < moves.size(); ++j) {
                if (i == j) {
                    continue;
                }

                Move n = moves[j];

                if (piece_at[move_from(m)] != piece_at[move_from(n)]) {
                    continue;
                }

                auto [f2, r2] = square_alg(move_from(n));

                if (f1 != f2) {
                    need_file = true;
                }
                else if (r1 != r2) {
                    need_rank = true;
                }
            }

            auto [to_file, to_rank] = square_alg(move_to(m));

            std::string promotion_str = "";

            if (move_type(m) == MOVE_PROMOTION) {
                Piece piece = move_end_piece(m);
                promotion_str = std::format("={}", piece_alg_table[piece]);
            }

            std::string capture_str = is_capture ? "x" : "";
            std::string unambig_file = need_file ? std::format("{}", f1) : "";
            std::string unambig_rank = need_rank ? std::format("{}", r1) : "";

            std::string check_suffix = "";

            make_move(m);
            if (is_checked[to_move]) {
                MoveList new_moves = generate_moves();
                filter_moves(new_moves);
                if (new_moves.count == 0) {
                    check_suffix = "#";
                }
                else {
                    check_suffix = "+";
                }
            }
            unmake_move();

            std::string name = "";

            switch (move_type(m)) {
                case MOVE_SHORT_CASTLE:
                    name = "O-O";
                    break;
                case MOVE_LONG_CASTLE:
                    name = "O-O-O";
                    break;
                default:
                    name = std::format("{}{}{}{}{}{}{}{}", piece_alg_table[piece_at[move_from(m)]], unambig_file, unambig_rank, capture_str, to_file, to_rank, promotion_str, check_suffix);
                    break;
            }

            names.emplace(std::move(name), m);
        } 
    }

    return names;
}

// hopefully these functions get inlined and the rewrites get optimized away
inline void remove_piece(Position& pos, int side, Piece piece, size_t index) {
    assert(piece);
    assert((Piece)pos.piece_at[index] == piece);
    assert(pos.sides[side].bb[piece] & sq_to_bb(index));
    pos.sides[side].bb[piece] &= ~sq_to_bb(index);
    pos.piece_at[index] = PIECE_NONE;
}

inline void set_piece(Position& pos, int side, Piece piece, size_t index) {
    assert(piece);
    assert((Piece)pos.piece_at[index] == PIECE_NONE);
    pos.sides[side].bb[piece] |= sq_to_bb(index);
    pos.piece_at[index] = (uint8_t)piece;
}

int get_captured_square(int to, MoveType ty, int side) {
    int captured_pos = to; // usually the piece being captured is at the square being moved to

    int offsets[] = {
        0, 0, -8, 8
    };

    int is_ep = ty == MOVE_EN_PASSANT;
    captured_pos = to + offsets[is_ep * 2 + side];

    return captured_pos;
}

void Position::update_en_passant_sq(int sq) {
#ifdef ZOBRIST_INCLUDE_EN_PASSANT_SQ
    zobrist ^= zobrist_table.ep(en_passant_sq);
#endif

    en_passant_sq = sq;

#ifdef ZOBRIST_INCLUDE_EN_PASSANT_SQ
    zobrist ^= zobrist_table.ep(en_passant_sq);
#endif
}

void Position::make_move(Move move) {
    // Update eval
    uint64_t initial_zobrist = zobrist;
    int32_t initial_eval = incr_eval;
    int initial_half_move_clock = half_move_clock;

    // First, check for a capture and remove the piece
    Piece captured_piece = move_captured_piece(move);
    assert(captured_piece != PIECE_KING);

    // ZOBRIST, remove any captured piece
#ifdef ZOBRIST_INCLUDE_PIECES
    int captured_pos = move_captured_square(move);
    zobrist ^= bool_to_mask<uint64_t>(captured_piece != PIECE_NONE) & zobrist_table.piece[opponent(to_move)][captured_piece][captured_pos];
#endif

    // Before we modify anything, record the destroyable data in the undo stack
    Undo undo = {
        .flags = flags,
        .move = move,
        .en_passant_sq = en_passant_sq,
        .incremental_eval = initial_eval,
        .zobrist = initial_zobrist,
        .is_checked = {
            is_checked[0],
            is_checked[1]
        },
        .half_move_clock = initial_half_move_clock,
    };

    undo_stack.push_back(undo);

    // remove the captured piece
    uint64_t capture_mask = bool_to_mask<uint64_t>(captured_piece != PIECE_NONE) & sq_to_bb(captured_pos);
    sides[opponent(to_move)].bb[captured_piece] ^= capture_mask;
    piece_at[captured_pos] = PIECE_NONE; // if no captured piece, this is okay because it will be updated with the moving piece

    // move the piece
    Piece start_piece = (Piece)piece_at[move_from(move)];
    Piece end_piece = move_end_piece(move);

    // update zobrist for moving piece
#ifdef ZOBRIST_INCLUDE_PIECES
    zobrist ^= zobrist_table.piece[to_move][start_piece][move_from(move)];
    zobrist ^= zobrist_table.piece[to_move][end_piece][move_to(move)];
#endif

    uint64_t from_mask = sq_to_bb(move_from(move));
    uint64_t to_mask = sq_to_bb(move_to(move));

    sides[to_move].bb[start_piece] ^= from_mask;
    sides[to_move].bb[end_piece]   ^= to_mask;

    piece_at[move_from(move)] = PIECE_NONE;
    piece_at[move_to(move)] = static_cast<uint8_t>(end_piece);

    // Update castling rights when a rook or a king is moved, or a rook is taken

    uint32_t remove_flags = 0;

    uint32_t flag_of_captured_rook = rook_castle_flag_table[opponent(to_move)][captured_pos];
    remove_flags |= bool_to_mask<uint32_t>(captured_piece == PIECE_ROOK) & flag_of_captured_rook;

    uint32_t flag_of_king_move = to_move == WHITE ? (POSITION_FLAG_WHITE_QCASTLE | POSITION_FLAG_WHITE_KCASTLE) : (POSITION_FLAG_BLACK_QCASTLE | POSITION_FLAG_BLACK_KCASTLE);
    remove_flags |= bool_to_mask<uint32_t>(start_piece == PIECE_KING) & flag_of_king_move;

    uint32_t flag_of_rook_move = rook_castle_flag_table[to_move][move_from(move)];
    remove_flags |= bool_to_mask<uint32_t>(start_piece == PIECE_ROOK) & flag_of_rook_move;

    uint32_t diff = remove_flags & flags;
    (void)diff;
    flags &= ~remove_flags;

#ifdef ZOBRIST_INCLUDE_FLAGS
    zobrist ^= zobrist_table.flags[diff];
#endif

    // set the en passant square if a double push has occured
    int en_passant_table[] = {
        NULL_SQUARE, NULL_SQUARE, move_to(move) - 8, move_to(move) + 8
    };

    bool is_double_push = move_type(move) == MOVE_DOUBLE_PUSH;

    update_en_passant_sq(en_passant_table[is_double_push*2 + to_move]);

    // move the rook when a castle is performed
    bool is_castle = move_type(move) == MOVE_SHORT_CASTLE || move_type(move) == MOVE_LONG_CASTLE;

    int rook_from = rook_jump_from[move_to(move)];
    int rook_to   = is_castle ? rook_jump_to[move_to(move)] : rook_from;

    uint64_t rook_from_mask = sq_to_bb(rook_from);
    uint64_t rook_to_mask   = sq_to_bb(rook_to);
    sides[to_move].bb[PIECE_ROOK] ^= rook_from_mask ^ rook_to_mask;
    
    std::swap(piece_at[rook_from], piece_at[rook_to]); // if not castle, it swaps the same position, so no change

#ifdef ZOBRIST_INCLUDE_FLAGS
    zobrist ^= zobrist_table.piece[to_move][PIECE_ROOK][rook_from];
    zobrist ^= zobrist_table.piece[to_move][PIECE_ROOK][rook_to]; // if not castle, same square so net zero change
#endif

#ifdef USE_NNUE
    accumulator_stack.emplace_back(accumulator_stack.back()); // push an accumulator onto the stack
#endif
    update_eval(captured_piece, captured_pos, start_piece, end_piece, move_from(move), move_to(move), rook_from, rook_to, to_move);

    // update to_move
    
    to_move = opponent(to_move);

#ifdef ZOBRIST_INCLUDE_SIDE
    zobrist ^= zobrist_table.side;
#endif

    update_is_checked();

    // update half-move clock

    bool is_capture = captured_piece != PIECE_NONE;
    bool is_pawn_move = start_piece == PIECE_PAWN;

    if (is_capture || is_pawn_move) {
        half_move_clock = 0;
    }
    else {
        half_move_clock++;
    }
}

void Position::update_is_checked() {
    assert(std::popcount(sides[WHITE].bb[PIECE_KING]) == 1);
    assert(std::popcount(sides[BLACK].bb[PIECE_KING]) == 1);
    is_checked[WHITE] = is_king_square_attacked(WHITE, std::countr_zero(sides[WHITE].bb[PIECE_KING]));
    is_checked[BLACK] = is_king_square_attacked(BLACK, std::countr_zero(sides[BLACK].bb[PIECE_KING]));
}

void Position::unmake_move() {
    Undo undo = undo_stack.back();
    undo_stack.pop_back();

    assert(undo.move != NULL_MOVE);

    Move move = undo.move;

    is_checked[WHITE] = undo.is_checked[WHITE];
    is_checked[BLACK] = undo.is_checked[BLACK];

    to_move = opponent(to_move);

    // move the rook if a castle has to be undone

    bool is_castle = move_type(move) == MOVE_SHORT_CASTLE || move_type(move) == MOVE_LONG_CASTLE;

    int rook_from = rook_jump_from[move_to(move)];
    int rook_to   = is_castle ? rook_jump_to[move_to(move)] : rook_from;

    uint64_t rook_from_mask = sq_to_bb(rook_from);
    uint64_t rook_to_mask   = sq_to_bb(rook_to);
    sides[to_move].bb[PIECE_ROOK] ^= rook_from_mask ^ rook_to_mask;
    
    std::swap(piece_at[rook_from], piece_at[rook_to]);

    // move the piece

    Piece end_piece = (Piece)piece_at[move_to(move)];
    Piece start_piece = move_type(move) == MOVE_PROMOTION ? PIECE_PAWN : end_piece;

    uint64_t to_mask   = sq_to_bb(move_to(move));
    uint64_t from_mask = sq_to_bb(move_from(move));

    sides[to_move].bb[end_piece] ^= to_mask;
    sides[to_move].bb[start_piece] ^= from_mask;

    piece_at[move_to(move)] = PIECE_NONE;
    piece_at[move_from(move)] = static_cast<uint8_t>(start_piece);

    // replace the captured piece
    int captured_square = move_captured_square(move);
    Piece captured_piece = move_captured_piece(move);

    uint64_t captured_mask = bool_to_mask<uint64_t>(captured_piece != PIECE_NONE) & sq_to_bb(captured_square);
    sides[opponent(to_move)].bb[captured_piece] ^= captured_mask;
    piece_at[captured_square] = static_cast<uint8_t>(captured_piece);

#ifdef USE_NNUE
    accumulator_stack.pop_back(); // restore the previous accumulator
#endif
    incr_eval = undo.incremental_eval;

    flags = undo.flags;
    en_passant_sq = undo.en_passant_sq;
    zobrist = undo.zobrist;
    half_move_clock = undo.half_move_clock;
}

void Position::make_null_move() {
    undo_stack.push_back(Undo {
        .flags = flags,
        .move = NULL_MOVE,
        .en_passant_sq = en_passant_sq,
        .incremental_eval = incr_eval,
        .zobrist = zobrist,
        .is_checked = {
            is_checked[0],
            is_checked[1]
        },
        .half_move_clock = half_move_clock
    });

    update_en_passant_sq(NULL_SQUARE);
    to_move = opponent(to_move);
#ifdef ZOBRIST_INCLUDE_SIDE
    zobrist ^= zobrist_table.side;
#endif

    update_is_checked();

#ifdef USE_NNUE
    accumulator_stack.emplace_back(accumulator_stack.back()); // push an accumulator onto the stack
#endif
}

void Position::unmake_null_move(){
    to_move = opponent(to_move);

    Undo undo = undo_stack.back();
    undo_stack.pop_back();

#ifdef USE_NNUE
    accumulator_stack.pop_back(); // restore the previous accumulator
#endif

    incr_eval = undo.incremental_eval;
    flags = undo.flags;
    en_passant_sq = undo.en_passant_sq;
    zobrist = undo.zobrist;
    half_move_clock = undo.half_move_clock;
}

uint64_t Position::generate_pin_mask(int side) const {
    int king_sq = get_king_sq(side);

    uint64_t opps = sides[opponent(side)].all();
    uint64_t allies = sides[side].all();

    uint64_t diags     = bishop_moves(king_sq, opps, 0); // excluding all our own pieces
    uint64_t straights =   rook_moves(king_sq, opps, 0); // excluding all our own pieces

    uint64_t rooks   = sides[opponent(side)].bb[PIECE_ROOK];
    uint64_t bishops = sides[opponent(side)].bb[PIECE_BISHOP];
    uint64_t queens  = sides[opponent(side)].bb[PIECE_QUEEN];

    uint64_t attackers = (diags & (bishops|queens)) | (straights & (rooks|queens));

    uint64_t pinned = 0;

    for (int atk_sq : set_bits(attackers)) {
        uint64_t mask = between[atk_sq][king_sq];
        uint64_t blockers = mask & allies;

        if (std::popcount(blockers) == 1) { // that guy is pinned
            pinned |= sq_to_bb(std::countr_zero(blockers));
        }
    }

    return pinned;
}

std::string to_uci_move(Move move) {
    int from = move_from(move);
    int to = move_to(move);

    char from_f = char(from & 7) + 'a';
    char from_r = char(from >> 3) + '1';
    
    char to_f = char(to & 7) + 'a';
    char to_r = char(to >> 3) + '1';

    std::string result = std::format("{}{}{}{}", from_f, from_r, to_f, to_r);

    if (move_type(move) == MOVE_PROMOTION) {
        switch (move_end_piece(move)) {
            default:
                break;
            case PIECE_QUEEN:
                result.push_back('q');
                break;
            case PIECE_KNIGHT:
                result.push_back('n');
                break;
            case PIECE_BISHOP:
                result.push_back('b');
                break;
            case PIECE_ROOK:
                result.push_back('r');
                break;
        }
    }

    return result;
}

bool Position::is_move_legal_slow(Move move) {
    MoveList moves = generate_moves();
    filter_moves(moves);

    for (Move mv : moves) {
        if (mv == move) {
            return true;
        }
    } 

    return false;
}

bool Position::is_quiescent() {
    if (is_checked[to_move]) {
        return false;
    }

    MoveList captures = generate_captures();
    filter_moves(captures);

    return captures.count == 0;
}