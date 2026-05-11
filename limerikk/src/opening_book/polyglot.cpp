#include "polyglot_random.h"
#include "limerikk.h"
#include "opening_book.h"

#include <fstream>
#include <iostream>
#include <random>
#include <cstdio>    
#include <cstdint>
#include <vector>

#include <bit>

uint16_t byteswap_u16(uint16_t val) {
    #if defined(_MSC_VER)
        return _byteswap_ushort(val); //
    #elif defined(__GNUC__)
        return __builtin_bswap16(val); //
    #else
    #error
    #endif
}

uint32_t byteswap_u32(uint32_t val) {
    #if defined(_MSC_VER)
        return _byteswap_ulong(val); //
    #elif defined(__GNUC__)
        return __builtin_bswap32(val); //
    #else
    #error
    #endif
}

uint64_t byteswap_u64(uint64_t val) {
    #if defined(_MSC_VER)
        return _byteswap_uint64(val); //
    #elif defined(__GNUC__)
        return __builtin_bswap64(val); //
    #else
    #error
    #endif
}


uint64_t to_big_endian(uint64_t x) {
    if constexpr (std::endian::native == std::endian::little)
        return byteswap_u64(x);
    else
        return x;
}

// Position Methods

/** 
 Encodes the polyglot hash key to use polyglot openning books
*/
uint64_t Position::encode_polyglot() {
    uint64_t piece_hash = 0;
    uint64_t castle_hash = 0;
    uint64_t enpassant_hash = 0;
    uint64_t turn_hash = 0;

    int piece_values[] = { -1, 0, 6, 2, 4, 8, 10 };

    // 1. Piece Hashing
    for (int sq = 0; sq < 64; ++sq) {
        int piece = piece_at[sq];
        
        if (piece == PIECE_NONE) {
            continue;
        }

        int piece_val = piece_values[piece];
        
        // If white +1 the value of the piece for different index
        if (sq_to_bb(sq) & sides[WHITE].all()) {
            piece_val += 1;
        }

        int row = sq / 8;
        int file = sq % 8;
        int index = (64 * piece_val) + (8 * row) + file;
        
        piece_hash ^= POLYGLOT_RANDOM[index];
    }

    // 2. Castling Hashing
    int castle_offset = 768;
    if (flags & POSITION_FLAG_WHITE_KCASTLE) castle_hash ^= POLYGLOT_RANDOM[castle_offset + 0];
    if (flags & POSITION_FLAG_WHITE_QCASTLE) castle_hash ^= POLYGLOT_RANDOM[castle_offset + 1];
    if (flags & POSITION_FLAG_BLACK_KCASTLE) castle_hash ^= POLYGLOT_RANDOM[castle_offset + 2];
    if (flags & POSITION_FLAG_BLACK_QCASTLE) castle_hash ^= POLYGLOT_RANDOM[castle_offset + 3];

    // 3. En Passant Hashing
    if (en_passant_sq) {
        
        // Polyglot only applies enpassant hash if the pawn is capturable
        bool can_capture = false;
        int ep_file = en_passant_sq % 8;
        
        // Check left
        if (ep_file > 0) {
            int left = en_passant_sq + (to_move == WHITE ? -9 : 7);
            if (piece_at[left] == PIECE_PAWN && (sq_to_bb(left) & sides[to_move].all())) 
                can_capture = true;
        }

        // Check right
        if (ep_file < 7) {
            int right = en_passant_sq + (to_move == WHITE ? -7 : 9);
            if (piece_at[right] == PIECE_PAWN && (sq_to_bb(right) & sides[to_move].all())) 
                can_capture = true;
        }

        if (can_capture) {
            enpassant_hash ^= POLYGLOT_RANDOM[772 + ep_file];
        }
    }

    if (to_move == WHITE) {
        turn_hash ^= POLYGLOT_RANDOM[780];
    }

    return (piece_hash ^ castle_hash ^ enpassant_hash ^ turn_hash);
}


Move Position::decode_polyglot(PolyglotEntry move) {
    uint16_t m = move.move;
    int to = m & 0x3F;
    int from = (m >> 6) & 0x3F;
    int prom = (m >> 12) & 7;

    Piece end_piece = static_cast<Piece>(piece_at[from]);
    MoveType type = MOVE_NORMAL;
    switch(prom) {
        case 1: end_piece = PIECE_KNIGHT; type = MOVE_PROMOTION; break;
        case 2: end_piece = PIECE_BISHOP; type = MOVE_PROMOTION; break;
        case 3: end_piece = PIECE_ROOK;   type = MOVE_PROMOTION; break;
        case 4: end_piece = PIECE_QUEEN;  type = MOVE_PROMOTION; break;
    }

    // Catch castle moves
    uint64_t to_bb = sq_to_bb(to);
    uint64_t from_bb = sq_to_bb(from);

    if ((from_bb & (FILE_E & RANK_1)) && (to_bb & (FILE_G & RANK_1))) {
        type = MOVE_SHORT_CASTLE;
    } else if ((from_bb & (FILE_E & RANK_1)) && (to_bb & (FILE_C & RANK_1))) {
        type = MOVE_LONG_CASTLE;
    } else if ((from_bb & (FILE_E & RANK_8)) && (to_bb & (FILE_G & RANK_8))) {
        type = MOVE_SHORT_CASTLE;
    } else if ((from_bb & (FILE_E & RANK_8)) && (to_bb & (FILE_C & RANK_8))) {
        type = MOVE_LONG_CASTLE;
    }

    // Catch en passant moves
    if (to == en_passant_sq) {
        type = MOVE_EN_PASSANT;
    }

    // Catch double pushes
    uint64_t starting_rank = to_move == WHITE ? RANK_2 : RANK_7;
    uint64_t ending_rank = to_move == WHITE ? RANK_4 : RANK_5;

    if ((end_piece == PIECE_PAWN) && (starting_rank & from_bb) && (ending_rank & to_bb)) {
        type = MOVE_DOUBLE_PUSH;
    }

    // Get captured piece
    int captured_sq = get_captured_square(to, type, to_move);
    Piece captured = static_cast<Piece>(piece_at[captured_sq]);

    return encode_move(from, to, type, end_piece, to_move, captured);
}



PolyglotEntry read_entry(uint64_t index) {
    const PolyglotEntry* entries = reinterpret_cast<const PolyglotEntry*>(OPENING_BOOK);
    PolyglotEntry e = entries[index];

    // convert big endian to little endian
    // TODO: Check if windows or linux system.
    e.key = byteswap_u64(e.key);
    e.move = byteswap_u16(e.move);
    e.weight = byteswap_u16(e.weight);
    e.learn = byteswap_u32(e.learn);

    return e;
}

uint64_t find_first(uint64_t key, uint64_t num_entries) {

    uint64_t low = 0;
    uint64_t high = num_entries;

    while (low < high) {
        uint64_t mid = low + (high - low) / 2;
        PolyglotEntry e = read_entry(mid);

        if (e.key < key) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return low;
}

std::vector<PolyglotEntry> probe_book(uint64_t key) {

    uint64_t num_entries =OPENING_BOOK_SIZE / sizeof(PolyglotEntry);
    uint64_t idx = find_first(key, num_entries);
    std::vector<PolyglotEntry> moves;

    while (idx < num_entries) {

        PolyglotEntry e = read_entry(idx);

        if (e.key != key) {
            break;
        }

        moves.push_back(e);
        ++idx;
    }

    return moves;
}

PolyglotEntry choose_move(const std::vector<PolyglotEntry>& moves) {

    // Safety check
    if (moves.empty()) {
            return {}; 
        }

    uint32_t total = 0;

    for (const auto& m : moves) {
        total += m.weight;
    }

    // Create rng
    static std::random_device rd;  
    static std::mt19937 gen(rd());

    // If all moves have 0 weight, pick one uniformly at random
    if (total == 0) {
        std::uniform_int_distribution<size_t> dist(0, moves.size() - 1);
        return moves[dist(gen)];
    }

    // Normal weighted selection
    std::uniform_int_distribution<uint32_t> dist(0, total - 1);
    uint32_t r = dist(gen);

    for (const auto& m : moves) {
        if (r < m.weight) {
            return m;
        }
        r -= m.weight;
    }

    return moves[0];
}
