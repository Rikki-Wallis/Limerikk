#include <cassert>
#include <cctype>

#include "limerikk.h"
#include "common.h"

NullBudgeter null_budgeter = NullBudgeter();

enum SideFlags {
    SIDE_FLAG_NONE = 0,
    SIDE_FLAG_CAN_CASTLE_KINGSIDE  = (1 << 0),
    SIDE_FLAG_CAN_CASTLE_QUEENSIDE = (1 << 1),
};

uint64_t Side::all() const {
    return bb[PIECE_PAWN] | bb[PIECE_ROOK] | bb[PIECE_KNIGHT] | bb[PIECE_BISHOP] | bb[PIECE_QUEEN] | bb[PIECE_KING];
}

void Position::display(bool display_metadata) const {
    verify_integrity();

    for (int rank=7; rank>=0; --rank) 
    {   
        print("{}|", rank+1);

        for (int file = 0; file < 8; ++file) 
        {
            int sq = rank * 8 + file;
            const char* piece = ".";
            uint64_t mask = sq_to_bb(sq);

            if      (sides[WHITE].bb[PIECE_PAWN]   & mask) piece = "P";
            else if (sides[WHITE].bb[PIECE_ROOK]   & mask) piece = "R";
            else if (sides[WHITE].bb[PIECE_KNIGHT] & mask) piece = "N";
            else if (sides[WHITE].bb[PIECE_BISHOP] & mask) piece = "B";
            else if (sides[WHITE].bb[PIECE_KING]   & mask) piece = "K";
            else if (sides[WHITE].bb[PIECE_QUEEN]  & mask) piece = "Q";

            if      (sides[BLACK].bb[PIECE_PAWN]   & mask) piece = "p";
            else if (sides[BLACK].bb[PIECE_ROOK]   & mask) piece = "r";
            else if (sides[BLACK].bb[PIECE_KNIGHT] & mask) piece = "n";
            else if (sides[BLACK].bb[PIECE_BISHOP] & mask) piece = "b";
            else if (sides[BLACK].bb[PIECE_KING]   & mask) piece = "k";
            else if (sides[BLACK].bb[PIECE_QUEEN]  & mask) piece = "q";

            print("{} ", piece);

        }

        print("\n");
    }

    print("  ---------------\n");
    print("  a b c d e f g h\n");

    if (display_metadata) {
        if (flags & POSITION_FLAG_WHITE_KCASTLE) {
            print("White can castle kingside.\n");
        }

        if (flags & POSITION_FLAG_WHITE_QCASTLE) {
            print("White can castle queenside.\n");
        }

        if (flags & POSITION_FLAG_BLACK_KCASTLE) {
            print("Black can castle kingside.\n");
        }

        if (flags & POSITION_FLAG_BLACK_QCASTLE) {
            print("Black can castle queenside.\n");
        }

        if (en_passant_sq != NULL_SQUARE) {
            auto [file, rank] = square_alg(en_passant_sq);
            print("En passant possible on {}{}.\n", file, rank);
        }
    }

    print("To move: {}\n", to_move == WHITE ? "white" : "black");
}

std::optional<Position> Position::parse_fen(const std::string& fen) {
    size_t cursor = 0;

    Position pos;

    auto set_piece = [&](int side, uint8_t piece, size_t rank, size_t file) {
        size_t index = rank * 8 + file;
        pos.sides[side].bb[piece] |= sq_to_bb(index);
        pos.piece_at[index] = piece; 
    };

    auto skip_whitespace = [&]() {
        while (cursor < fen.size() && isspace(fen[cursor])) {
            cursor++;
        }
    };

    auto next = [&]() {
        return fen[cursor++];
    };
    
    auto peek = [&]() {
        return fen[cursor];
    };

    // Piece placement

    skip_whitespace();
    
    for (int rank = 7; rank >= 0; --rank) {
        if (rank < 7) {
            if (cursor >= fen.size()) return std::nullopt;
            if (next() != '/') return std::nullopt;
        }

        size_t file = 0;

        while (file < 8) {
            if (cursor >= fen.size()) return std::nullopt;
            char c = next();

            int side = isupper(c) == 0;

            switch (c) {
                default:
                    return std::nullopt;

                case 'p':
                case 'P':
                    set_piece(side, PIECE_PAWN, rank, file);
                    file++;
                    break;
                case 'r':
                case 'R':
                    set_piece(side, PIECE_ROOK, rank, file);
                    file++;
                    break;
                case 'n':
                case 'N':
                    set_piece(side, PIECE_KNIGHT, rank, file);
                    file++;
                    break;
                case 'b':
                case 'B':
                    set_piece(side, PIECE_BISHOP, rank, file);
                    file++;
                    break;
                case 'q':
                case 'Q':
                    set_piece(side, PIECE_QUEEN, rank, file);
                    file++;
                    break;
                case 'k':
                case 'K':
                    set_piece(side, PIECE_KING, rank, file);
                    file++;
                    break;

                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                    file += c - '0';
                    break;
            }
        }
    }

    // To move

    skip_whitespace();
    if (cursor >= fen.size()) return std::nullopt;

    switch (next()) {
        default:
            return std::nullopt;
        case 'w': 
            pos.to_move = WHITE;
            break;
        case 'b': 
            pos.to_move = BLACK;
            break;
    }

    // Castling ability

    skip_whitespace();
    if (cursor >= fen.size()) return std::nullopt;

    if (peek() != '-') {
        while (cursor < fen.size() && !isspace(peek())) {
            char c = next();

            switch(c) {
                default:
                    return std::nullopt;

                case 'k':
                    pos.flags |= POSITION_FLAG_BLACK_KCASTLE;
                    break;
                case 'K':
                    pos.flags |= POSITION_FLAG_WHITE_KCASTLE;
                    break;
                case 'q':
                    pos.flags |= POSITION_FLAG_BLACK_QCASTLE;
                    break;
                case 'Q':
                    pos.flags |= POSITION_FLAG_WHITE_QCASTLE;
                    break;
            }
        }
    }
    else {
        next();
    }

    // En passant square

    skip_whitespace();
    if (cursor >= fen.size()) return std::nullopt;

    if (peek() != '-') {
        int file = next() - 'a';
        if (cursor >= fen.size()) return std::nullopt;

        int rank = next() - '1'; 
        if (rank > 7 || rank < 0) {
            return std::nullopt;
        }

        pos.en_passant_sq = rank * 8 + file;
    }
    else {
        next();
    }

    // Half-move clock

    skip_whitespace();
    if (cursor >= fen.size()) return std::nullopt;

    int half_move_clock = 0;

    while (cursor < fen.size() && isdigit(peek())) {
        char c = next();
        half_move_clock *= 10;
        half_move_clock += c - '0';
    }

    pos.zobrist = pos.compute_zobrist();
    pos.update_is_checked();
    pos.half_move_clock = half_move_clock;

#ifdef USE_NNUE
    pos.init_nnue_accumulator();
#endif

    pos.incr_eval = pos.compute_eval();

    return pos;
}

std::string Position::fen() const {
    std::string result{};

    uint64_t white_pieces = sides[WHITE].all(); 

    for (int r = 7; r >= 0; --r) {
        int f = 0;

        if (r < 7) {
            result.push_back('/');
        }

        while (f < 8) {
            int sq = r*8+f;
            bool is_white = (sq_to_bb(sq) & white_pieces) != 0;
            switch (piece_at[sq]) {
                case PIECE_PAWN:
                    result.push_back(is_white ? 'P' : 'p');
                    f++;
                    break;
                case PIECE_ROOK:
                    result.push_back(is_white ? 'R' : 'r');
                    f++;
                    break;
                case PIECE_KNIGHT:
                    result.push_back(is_white ? 'N' : 'n');
                    f++;
                    break;
                case PIECE_BISHOP:
                    result.push_back(is_white ? 'B' : 'b');
                    f++;
                    break;
                case PIECE_KING:
                    result.push_back(is_white ? 'K' : 'k');
                    f++;
                    break;
                case PIECE_QUEEN:
                    result.push_back(is_white ? 'Q' : 'q');
                    f++;
                    break;

                case PIECE_NONE: {
                    int start = f;
                    while (f < 8 && piece_at[r*8+f] == PIECE_NONE) {
                        f++;
                    }
                    result.push_back(char(f-start) + '0');
                } break;
            }
        }
    }

    result.push_back(' ');
    result.push_back(to_move == WHITE ? 'w' : 'b');

    // castling rights
    result.push_back(' ');
    if (flags == 0) {
        result.push_back('-');
    }
    else {
        if (flags & POSITION_FLAG_WHITE_KCASTLE) {
            result.push_back('K');
        }

        if (flags & POSITION_FLAG_WHITE_QCASTLE) {
            result.push_back('Q');
        }

        if (flags & POSITION_FLAG_BLACK_KCASTLE) {
            result.push_back('k');
        }

        if (flags & POSITION_FLAG_BLACK_QCASTLE) {
            result.push_back('q');
        }
    }

    result.push_back(' ');
    if (en_passant_sq == NULL_SQUARE) {
        result.push_back('-');
    }
    else {
        auto [file, rank] = square_alg(en_passant_sq);
        result.push_back(file);
        result.push_back(char(rank) + '0');
    }

    result.push_back(' ');

    result += std::format("{}", half_move_clock);

    result.push_back(' ');
    result.push_back('1');

    return result;
}

uint64_t Position::all_pieces() const {
    return sides[0].all() | sides[1].all();
}

std::vector<Side> Position::get_sides() const {
    return std::vector<Side>(sides, sides + 2);
}

std::vector<uint64_t> Side::get_bbs() const {
    return std::vector<uint64_t>(bb, bb + NUM_PIECE_TYPES);
}

static void fill_map(uint8_t* map, uint64_t bb, uint8_t value) {
    assert(value);

    for (int i = 0; i < 64; ++i) {
        if (bb >> i & 1) {
            assert(!map[i]);
            map[i] = value;
        }
    }
}

void Position::verify_integrity() const {
    uint8_t map[64] = {};

    for (const Side& side : sides) {
        fill_map(map, side.bb[PIECE_PAWN],   PIECE_PAWN);
        fill_map(map, side.bb[PIECE_ROOK],   PIECE_ROOK);
        fill_map(map, side.bb[PIECE_KNIGHT], PIECE_KNIGHT);
        fill_map(map, side.bb[PIECE_BISHOP], PIECE_BISHOP);
        fill_map(map, side.bb[PIECE_QUEEN],  PIECE_QUEEN);
        fill_map(map, side.bb[PIECE_KING],   PIECE_KING);
    }

    assert(memcmp(map, piece_at, sizeof(map)) == 0);
}

int Position::get_king_sq(int side) const {
    assert(std::popcount(sides[side].bb[PIECE_KING]) == 1);
    return std::countr_zero(sides[side].bb[PIECE_KING]);
}

std::optional<GameResult> Position::game_result() {
    MoveList moves = generate_moves();
    filter_moves(moves);

    if (moves.count == 0) {
        if (is_checked[to_move]) {
            return GameResult {
                .result = to_move == WHITE ? -1 : 1,
                .reason = GAME_RESULT_CHECKMATE
            };
        }
        else {
            return GameResult {
                .result = 0,
                .reason = GAME_RESULT_STALEMATE
            };
        }
    }

    if (half_move_clock == 100) {
        return GameResult {
            .result = 0,
            .reason = GAME_RESULT_50_MOVE_RULE
        };
    }

    if (is_threefold_repetition()) {
        return GameResult {
            .result = 0,
            .reason= GAME_RESULT_3_FOLD_REPETITION
        };
    }

    return std::nullopt;
}

bool Position::is_threefold_repetition() const {
    int count = 0;

    for (int i = int(undo_stack.size())-2; i >= 0; i -= 2) {
        const Undo& undo = undo_stack[i];

        if (undo.zobrist == zobrist) {
            count++;
        }

        if (count >= 2) return true;

        if (move_captured_piece(undo.move) != PIECE_NONE) {
            break; // irreversible move - position cannot repeat across them
        }
    } 

    return false;
}

std::array<uint64_t, 12> Position::to_bitboards() const {
    std::array<uint64_t, 12> bbs;

    Piece pieces[6] = {
        PIECE_PAWN,
        PIECE_KNIGHT,
        PIECE_BISHOP,
        PIECE_ROOK,
        PIECE_QUEEN,
        PIECE_KING
    };

    for (int side = 0; side < 2; ++side) {
        for (int pid = 0; pid < 6; ++pid) {
            bbs[side*6+pid] = sides[side].bb[pieces[pid]];
        }
    }

    return bbs;
}

bool NullBudgeter::should_exit(int node_count) const {
    (void)node_count;
    return false;
}
