#include "limerikk.h"


int64_t Side::material_value() const {
    int64_t sum = 0;

    for (int p = PIECE_PAWN; p < NUM_PIECE_TYPES; ++p) {
        int n = std::popcount(bb[p]);
        sum += n * piece_value_table[p];
    }

    return sum;
}

// stolen from https://www.chessprogramming.org/Simplified_Evaluation_Function
static const int MG_PAWN_PST[64] = {
    0,  0,  0,  0,  0,  0,  0,  0,
    5, 10, 10,-20,-20, 10, 10,  5,
    5, -5,-10,  0,  0,-10, -5,  5,
    0,  0,  0, 20, 20,  0,  0,  0,
    5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
    0,  0,  0,  0,  0,  0,  0,  0,
};

static const int EG_PAWN_PST[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    10, 10, 10, 10, 10, 10, 10, 10,
    15, 15, 15, 20, 20, 15, 15, 15,
    20, 20, 25, 30, 30, 25, 20, 20,
    30, 30, 35, 40, 40, 35, 30, 30,
    40, 40, 45, 50, 50, 45, 40, 40,
    70, 70, 70, 70, 70, 70, 70, 70,
     0,  0,  0,  0,  0,  0,  0,  0
};

static const int KNIGHT_PST[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50,
};

static const int MG_BISHOP_PST[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20,
};

static const int EG_BISHOP_PST[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 15, 15, 15, 15, 10,-10,
    -10, 10, 15, 20, 20, 15, 10,-10,
    -10, 10, 15, 20, 20, 15, 10,-10,
    -10, 10, 15, 15, 15, 15, 10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};

static const int MG_ROOK_PST[64] = {
    0,  0,  0,  5,  5,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    5, 10, 10, 10, 10, 10, 10,  5,
    0,  0,  0,  0,  0,  0,  0,  0,
};

static const int EG_ROOK_PST[64] = {
     0,  5, 10, 15, 15, 10,  5,  0,
     0,  5, 10, 15, 15, 10,  5,  0,
     0,  5, 10, 15, 15, 10,  5,  0,
     0,  5, 10, 15, 15, 10,  5,  0,
     0,  5, 10, 15, 15, 10,  5,  0,
     0,  5, 10, 15, 15, 10,  5,  0,
     5, 10, 15, 20, 20, 15, 10,  5,
     0,  0,  5, 10, 10,  5,  0,  0
};

static const int MG_QUEEN_PST[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -10,  5,  5,  5,  5,  5,  0,-10,
      0,  0,  5,  5,  5,  5,  0, -5,
     -5,  0,  5,  5,  5,  5,  0, -5,
    -10,  0,  5,  5,  5,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20,
};

static const int EG_QUEEN_PST[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  5, 10, 15, 15, 10,  5,-10,
     -5, 10, 15, 20, 20, 15, 10, -5,
     -5, 10, 15, 20, 20, 15, 10, -5,
    -10,  5, 10, 15, 15, 10,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};

static const int MG_KING_PST[64] = {
    20, 30, 10,  0,  0, 10, 30, 20,
    20, 20,  0,  0,  0,  0, 20, 20,
    -10,-20,-20,-20,-20,-20,-20,-10,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
};

// stolen from https://github.com/SebLague/Chess-Coding-Adventure/blob/Chess-V2-UCI/Chess-Coding-Adventure/src/Core/Evaluation/PieceSquareTable.cs
static const int EG_KING_PST[64] = {
    -20, -10, -10, -10, -10, -10, -10, -20,
    -5,   0,   5,   5,   5,   5,   0,  -5,
    -10, -5,   20,  30,  30,  20,  -5, -10,
    -15, -10,  35,  45,  45,  35, -10, -15,
    -20, -15,  30,  40,  40,  30, -15, -20,
    -25, -20,  20,  25,  25,  20, -20, -25,
    -30, -25,   0,   0,   0,   0, -25, -30,
    -50, -30, -30, -30, -30, -30, -30, -50
};


static const int ZEROED_PST[64] = {};

static const int* MG_PST[NUM_PIECE_TYPES] = {
    ZEROED_PST,
    MG_PAWN_PST,
    MG_ROOK_PST,
    KNIGHT_PST,
    MG_BISHOP_PST,
    MG_QUEEN_PST,
    MG_KING_PST,
};

static const int* EG_PST[NUM_PIECE_TYPES] = {
    ZEROED_PST,
    EG_PAWN_PST,
    EG_ROOK_PST,
    KNIGHT_PST,
    EG_BISHOP_PST,
    EG_QUEEN_PST,
    EG_KING_PST,
};

static const int64_t SHELTER_STRENGTH_BONUS = 20;
static const int64_t SHELTER_STRENGTH_PENALTY = 20;
static const int64_t OPEN_FILE_BONUS = 20;
static const int64_t OPEN_FILE_PENALTY = 20;
static const int64_t Q_CASTLING_BONUS = 20;
static const int64_t K_CASTLING_BONUS = 20;
static const int64_t Q_CASTLING_PENALTY = 20;
static const int64_t K_CASTLING_PENALTY = 20;
static const int64_t BISHOP_PAIR_BONUS = 20;
static const int64_t ISOLATED_PAWN_PENALTY = 20;
static const int64_t DOUBLED_PAWN_PENALTY = 10; // This will trigger twice so half the actually value you want
static const int64_t CONNECTED_PAWN_BONUS = 5;
static const int64_t PASSED_PAWN_BONUS = 30;

int32_t mg_unsigned_pst_value(Piece piece, int square, int side) {
    return MG_PST[piece][square ^ (bool_to_mask<int>(side==BLACK) & 56)];
}

int32_t eg_unsigned_pst_value(Piece piece, int square, int side) {
    return EG_PST[piece][square ^ (bool_to_mask<int>(side==BLACK) & 56)];
}

int64_t Position::compute_eval() const {
    #ifdef USE_NNUE
        return nnue_eval();
    #else

    int64_t value = sides[WHITE].material_value() - sides[BLACK].material_value();

    /*
    // Compute phase
    int phase = 0;
    phase += std::popcount(sides[WHITE].bb[PIECE_KNIGHT] | sides[BLACK].bb[PIECE_KNIGHT]) * 1;
    phase += std::popcount(sides[WHITE].bb[PIECE_BISHOP] | sides[BLACK].bb[PIECE_BISHOP]) * 1;
    phase += std::popcount(sides[WHITE].bb[PIECE_ROOK]   | sides[BLACK].bb[PIECE_ROOK]  ) * 2;
    phase += std::popcount(sides[WHITE].bb[PIECE_QUEEN]  | sides[BLACK].bb[PIECE_QUEEN] ) * 4;
    phase = std::min(phase, 24);

    int mg_weight = static_cast<uint8_t>(phase);
    int eg_weight = 24 - static_cast<uint8_t>(phase);
    int mg_score = 0;
    int eg_score = 0;

    for (int p = PIECE_PAWN; p < NUM_PIECE_TYPES; ++p) {
        for (int sq : set_bits(sides[WHITE].bb[p])) {
            mg_score += mg_unsigned_pst_value((Piece)p, sq, WHITE);
            eg_score += eg_unsigned_pst_value((Piece)p, sq, WHITE);
        }
    }

    for (int p = PIECE_PAWN; p < NUM_PIECE_TYPES; ++p) {
        for (int sq : set_bits(sides[BLACK].bb[p])) {
            mg_score -= mg_unsigned_pst_value((Piece)p, sq, BLACK);
            eg_score -= eg_unsigned_pst_value((Piece)p, sq, BLACK);
        }
    }

    value += ((mg_score * mg_weight + eg_score * eg_weight) / 24);


    
    uint64_t white_pawns = sides[WHITE].bb[PIECE_PAWN]; 
    uint64_t black_pawns = sides[BLACK].bb[PIECE_PAWN];
    uint64_t white_king  = sides[WHITE].bb[PIECE_KING];
    uint64_t black_king  = sides[BLACK].bb[PIECE_KING];

    value += king_safety(WHITE, white_king, white_pawns);
    value += king_safety(BLACK, black_king, black_pawns);
    value += pawn_structure(WHITE, white_pawns);
    value += pawn_structure(BLACK, black_pawns);
    value += mobility(WHITE);
    value += mobility(BLACK);
    value += bishop_imbalance();
    */

    for (int p = PIECE_PAWN; p < NUM_PIECE_TYPES; ++p) {
        for (int sq : set_bits(sides[WHITE].bb[p])) {
            value += mg_unsigned_pst_value((Piece)p, sq, WHITE);
        }

        for (int sq : set_bits(sides[BLACK].bb[p])) {
            value -= mg_unsigned_pst_value((Piece)p, sq, BLACK);
        }
    }
    
    return value;
#endif
}


// TODO: Add pregenerated masks and whatnot
int64_t Position::king_safety(int colour, uint64_t king_bb, uint64_t pawn_bb) const {

    int64_t value = 0;
    /**  King Safety Eval
        1. Castling status
        2. Pawn shield
        3. Open files
    */

    // Bonuses & Penalties
    int64_t q_castling_bonus         = colour == WHITE ? Q_CASTLING_BONUS : -Q_CASTLING_BONUS;
    int64_t q_castling_penalty       = colour == WHITE ? Q_CASTLING_PENALTY : -Q_CASTLING_PENALTY;

    int64_t k_castling_bonus         = colour == WHITE ? K_CASTLING_BONUS : -K_CASTLING_BONUS;
    int64_t k_castling_penalty       = colour == WHITE ? K_CASTLING_PENALTY : -K_CASTLING_PENALTY; 

    int64_t shelter_strength_bonus   = colour == WHITE ? SHELTER_STRENGTH_BONUS : -SHELTER_STRENGTH_BONUS;
    int64_t shelter_strength_penalty = colour == WHITE ? SHELTER_STRENGTH_PENALTY : -SHELTER_STRENGTH_PENALTY;

    uint64_t open_file_bonus         = colour == WHITE ? OPEN_FILE_BONUS : -OPEN_FILE_BONUS;
    uint64_t open_file_penalty       = colour == WHITE ? OPEN_FILE_PENALTY : -OPEN_FILE_PENALTY;  

    // Pawn shelter vars
    uint64_t pawn_rank = colour == WHITE ? RANK_2 : RANK_7;
    uint64_t king_rank = colour == WHITE ? RANK_1 : RANK_8;
    uint64_t king_side = king_rank & (FILE_F | FILE_G | FILE_H);
    uint64_t queen_side = king_rank & (FILE_A | FILE_B | FILE_C);
    uint64_t null_side = king_rank & (FILE_D | FILE_E);
    uint64_t a_pawn = pawn_rank & FILE_A;
    uint64_t b_pawn = pawn_rank & FILE_B;
    uint64_t c_pawn = pawn_rank & FILE_C;
    uint64_t f_pawn = pawn_rank & FILE_F;
    uint64_t g_pawn = pawn_rank & FILE_G;
    uint64_t h_pawn = pawn_rank & FILE_H;


    // 1. Castling status
    int queen_side_flag = colour == WHITE ? POSITION_FLAG_WHITE_QCASTLE : POSITION_FLAG_BLACK_QCASTLE;
    int king_side_flag  = colour == WHITE ? POSITION_FLAG_WHITE_KCASTLE : POSITION_FLAG_BLACK_KCASTLE;

    if (flags & queen_side_flag) {
        value += q_castling_bonus;
    } else {
        value -= q_castling_penalty;
    }

    if (flags & king_side_flag) {
        value += k_castling_bonus;
    } else {
        value -= k_castling_penalty;
    }

    // 2. Pawn Shelter
    bool check_king = false;
    bool check_queen = false;
    if ((king_side & king_bb) || (null_side & king_bb)) {
        check_king = true;
    }
    if ((queen_side & king_bb) || (null_side & king_bb)) {
        check_queen = true;
    }

    // Kingside shelter
    if (!(check_king)) {

    } else if ((pawn_bb & f_pawn) && (pawn_bb & g_pawn) && (pawn_bb & h_pawn)) {
        value += shelter_strength_bonus;
    } else {
        value -= shelter_strength_penalty;
    }

    // Queenside shelter
    if (!(check_queen)) {
        
    } else if ((pawn_bb & a_pawn) && (pawn_bb & b_pawn) && (pawn_bb & c_pawn)) {
        value += shelter_strength_bonus;
    } else {
        value -= shelter_strength_penalty;
    }

    // 3. Open files near king
    uint64_t rank_left = bb_to_file(((king_bb << 1) & ~FILE_A));
    uint64_t rank_right = bb_to_file(((king_bb >> 1) & ~FILE_H));

    // For now only consider if the file is open due to no pawns
    if (rank_left & pawn_bb) {
        value += open_file_bonus;
    } else {
        value -= open_file_penalty;
    }   
    if (rank_right & pawn_bb) {
        value += open_file_bonus;
    } else {
        value -= open_file_penalty;
    }

    return value;
}


int64_t Position::pawn_structure(int colour, uint64_t ally_pawn_bb) const {

    int64_t value = 0;
    /** Pawn Structure Eval
        1. Isolated pawns
        2. Doubled pawns
        3. Passed pawns
        4. Connected pawns
     */
    
    // Bonuses & Penalties
    int64_t isolated_pawn_penalty = colour == WHITE ? ISOLATED_PAWN_PENALTY : -ISOLATED_PAWN_PENALTY;
    int64_t doubled_pawn_penalty  = colour == WHITE ? DOUBLED_PAWN_PENALTY : -DOUBLED_PAWN_PENALTY;
    int64_t passed_pawn_bonus     = colour == WHITE ? PASSED_PAWN_BONUS : -PASSED_PAWN_BONUS;
    int64_t connected_pawn_bonus  = colour == WHITE ? CONNECTED_PAWN_BONUS : -CONNECTED_PAWN_BONUS;

    // Useful vars
    uint64_t edge_file_left = colour == WHITE ? FILE_A : FILE_H;
    uint64_t edge_file_right = colour == WHITE ? FILE_H : FILE_A;
    uint64_t black_ranks[8] = {RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8};
    uint64_t white_ranks[8] = {RANK_8, RANK_7, RANK_6, RANK_5, RANK_4, RANK_3, RANK_2, RANK_1};   
    int opp_colour = colour == WHITE ? BLACK : WHITE;
    uint64_t opp_pawns = sides[opp_colour].bb[PIECE_PAWN];

    for (int sq : set_bits(ally_pawn_bb)) {
        uint64_t single_pawn_bb = sq_to_bb(sq);

        // 1. Isolated pawns
        uint64_t file_left = bb_to_file(((single_pawn_bb << 1) & ~edge_file_left));
        uint64_t file_right = bb_to_file(((single_pawn_bb >> 1) & ~edge_file_right));

        if ((!(file_left & ally_pawn_bb)) & (!(file_right & ally_pawn_bb))) {
            value -= isolated_pawn_penalty;
        }

        // 2. Doubled pawns
        if (single_pawn_bb & (ally_pawn_bb & ~single_pawn_bb)) {
            value -= doubled_pawn_penalty;
        }

        // 3. Passed Pawns
        uint64_t pawn_file = bb_to_file(single_pawn_bb);
        uint64_t passed_mask = pawn_file | file_left | file_right;        

        for (int i=0; i<8; ++i) {
            uint64_t rank = colour == WHITE ? white_ranks[i] : black_ranks[i];
            if (rank & single_pawn_bb) {
                value += passed_pawn_bonus;
            } else if ((rank & passed_mask) & opp_pawns) {
                break;
            }
        }
    }

    // 4. Connected Pawns TODO: Probably very buggy
    uint64_t left = (ally_pawn_bb << 1) & ~FILE_A;
    uint64_t right = (ally_pawn_bb >> 1) & ~FILE_H;
    uint64_t diag1 = colour == WHITE ? ((ally_pawn_bb << 7) & ~FILE_H) : ((ally_pawn_bb >> 7) & ~FILE_A);
    uint64_t diag2 = colour == WHITE ? ((ally_pawn_bb << 9) & ~FILE_A) : ((ally_pawn_bb >> 9) & ~FILE_H);
    uint64_t connected  = ally_pawn_bb & (left | right |diag1 | diag2);

    value += std::popcount(connected) * connected_pawn_bonus;

    return value;
}


int64_t Position::bishop_imbalance() const {

    uint64_t black_bishops = sides[BLACK].bb[PIECE_BISHOP];
    uint64_t white_bishops = sides[WHITE].bb[PIECE_BISHOP];

    int num_white = std::popcount(white_bishops);
    int num_black = std::popcount(black_bishops);
    int difference = num_white - num_black;

    if (difference > 0) {
        return BISHOP_PAIR_BONUS;
    } else if (difference < 0) {
        return -BISHOP_PAIR_BONUS;
    }

    return 0;
}

int64_t Position::mobility(int colour) const {
    int64_t value = 0;

    uint64_t allies = sides[colour].all();
    uint64_t all = all_pieces();


    for (int sq : set_bits(sides[colour].bb[PIECE_KNIGHT])) {
        uint64_t knight_attacks = knight_moves(sq, allies);
        value += std::popcount(knight_attacks);
    }

    for (int sq : set_bits(sides[colour].bb[PIECE_BISHOP])) {
        uint64_t bishop_attacks = bishop_moves(sq, all, allies);
        value += std::popcount(bishop_attacks);
    }

    for (int sq : set_bits(sides[colour].bb[PIECE_ROOK])) {
        uint64_t rook_attacks = rook_moves(sq, all, allies);
        value += std::popcount(rook_attacks);
    }

    for (int sq : set_bits(sides[colour].bb[PIECE_QUEEN])) {
        uint64_t queen_attacks = queen_moves(sq, all, allies);
        value += std::popcount(queen_attacks);
    }


    int64_t return_val = colour == WHITE ? value : -value;

    return return_val;
}


int64_t Position::signed_eval() {
    int64_t sign = to_move == WHITE ? 1 : -1;
    return incr_eval * sign;
}
 
inline int32_t piece_delta(Piece piece, int sq, int side) {
    int32_t sign = side == BLACK ? -1 : 1;
    int32_t value = 0;
    value += sign * mg_unsigned_pst_value(piece, sq, side);
    value += sign * piece_value_table[piece];
    return value;
}

#ifndef USE_NNUE
void Position::update_eval(Piece captured_piece, int captured_pos, Piece moving_piece_start, Piece moving_piece_end, int move_from, int move_to, int rook_from, int rook_to, int side, int sign) {
    (void)captured_piece;
    (void)captured_pos;
    (void)moving_piece_start;
    (void)moving_piece_end;
    (void)move_from;
    (void)move_to;
    (void)rook_from;
    (void)rook_to;
    (void)side;
    (void)sign;
    // NOTE: if rook_from == rook_to there is NO castle
    // ensure that if that is the case, your castling operations have a NET ZERO

    incr_eval -= sign * piece_delta(captured_piece, captured_pos, opponent(side));

    incr_eval -= sign * piece_delta(moving_piece_start, move_from, side);
    incr_eval += sign * piece_delta(moving_piece_end, move_to, side);

    // since these are symmetric, should have net zero when rook_from == rook_to
    incr_eval -= sign * piece_delta(PIECE_ROOK, rook_from, to_move);
    incr_eval += sign * piece_delta(PIECE_ROOK, rook_to, to_move);
}
#endif