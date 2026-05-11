#include <cstdio>
#include <vector>
#include <cstdint>
#include "limerikk.h"

constexpr ZobristTable initialize_zobrist_table();

const ZobristTable zobrist_table = initialize_zobrist_table();

typedef struct { uint64_t state;  uint64_t inc; } pcg32_random_t;

constexpr uint32_t pcg32_random_r(pcg32_random_t* rng)
{
    uint64_t oldstate = rng->state;
    // Advance internal state
    rng->state = oldstate * 6364136223846793005ULL + (rng->inc|1);
    // Calculate output function (XSH RR), uses old state for max ILP
    uint32_t xorshifted = uint32_t(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << ((-(int32_t)rot) & 31));
}

constexpr uint64_t rand64(pcg32_random_t* rng) {
    uint64_t a = pcg32_random_r(rng);
    uint64_t b = pcg32_random_r(rng);
    return (a << 32) | b;
}

constexpr ZobristTable initialize_zobrist_table() {
    // Create and seed rand num gen
    pcg32_random_t rng = {
        .state = 67,
        .inc = 3
    };

    ZobristTable table;

    // Set random numbers for each square for each piece
    for (int side = 0; side < 2; ++side) {
        for (int piece = PIECE_PAWN; piece < NUM_PIECE_TYPES; ++piece) {
            for (int sq = 0; sq < 64; ++sq) {
                table.piece[side][piece][sq] = rand64(&rng);
            }
        }
    }      

    // Set random number for black to move
    table.side = rand64(&rng);

    // Set random numbers for position flags

    for (int i = 0; i < 8; ++i) { // all singular bits
        table.flags[static_cast<uint64_t>(1) << i] = rand64(&rng);
    }

    for (size_t i = 0; i < std::size(table.flags); ++i) { // all combinations of singular bits
        uint64_t x = 0;

        for (int b : set_bits((uint64_t)i)) {
            x ^= table.flags[static_cast<uint64_t>(1) << b];
        }

        table.flags[i] = x;
    }

    table.flags[0] = 0; // if no flag changes, should be no change

    // Set random numbers for each square for enpassant
    for (size_t i = 0; i < std::size(table.ep_table); ++i) {
        table.ep_table[i] = rand64(&rng);
    }

    return table;
}

uint64_t Position::compute_zobrist() const {
    // Now we can begin forming the hash
    uint64_t hash = 0;

#ifdef ZOBRIST_INCLUDE_PIECES
    // Begin by going through each square and xoring into the hash
    uint64_t all_white = sides[WHITE].all();
    uint64_t all_black = sides[BLACK].all();

    for (int sq = 0; sq < 64; ++sq) {
        if (all_white & sq_to_bb(sq)) {
            hash ^= zobrist_table.piece[WHITE][piece_at[sq]][sq];
        } else if (all_black & sq_to_bb(sq)) {
            hash ^= zobrist_table.piece[BLACK][piece_at[sq]][sq];
        }
    }
#endif

#ifdef ZOBRIST_INCLUDE_FLAGS
    // xor with flags
    hash ^= zobrist_table.flags[flags];

#endif

#ifdef ZOBRIST_INCLUDE_SIDE
    // xor side if it is blacks turn to move
    if (to_move == BLACK) {
        hash ^= zobrist_table.side;
    }
#endif

#ifdef ZOBRIST_INCLUDE_EN_PASSANT_SQ
    hash ^= zobrist_table.ep(en_passant_sq);
#endif

    return hash;
}