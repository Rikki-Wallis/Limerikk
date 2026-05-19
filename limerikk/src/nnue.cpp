#include <cstdio>
#include <cmath>
#include <algorithm>
#include <bit>

#ifdef __AVX2__
#include <immintrin.h>
#elif defined(__ARM_FEATURE_MATMUL_INT8)
#include <arm_neon.h>
#else
//#error "No intrinsics supported for NNUE"
#endif

#include "limerikk.h"

#if 1
#include "nnue_embed.h"
#else
#error "Uncomment weight inclusion"
static int8_t nnue_w0[10][10];
static int16_t nnue_b0[10];
static int8_t nnue_w1[10][10];
static int32_t nnue_b1[10];
static int16_t nnue_w2[10][10];
static int32_t nnue_b2[10];
static const int NNUE_ACCUMULATOR_PERSP_SIZE = 64;
#endif

static_assert(std::size(nnue_b0) == NNUE_ACCUMULATOR_PERSP_SIZE);
static_assert(_ACCUMULATOR_PERSP_SIZE == NNUE_ACCUMULATOR_PERSP_SIZE);

template<typename A, typename B, size_t OUT_Q>
inline B scaled_crelu(A x, A q) {
    A sx = x*A(OUT_Q)/q;
    A lo = 0;
    A hi = OUT_Q;
    return B(std::clamp(sx, lo, hi));
}

inline float sigmoid(float x) {
    return 1.0f/(1+std::exp(-x));
}

inline float scaled_sigmoid(int32_t x, int32_t q) {
    float xf = float(x)/float(q);
    return sigmoid(xf);
}

struct ActiveIndices {
    int data[64];
    int count;
};

static int get_feature(int persp, int piece_side, int piece_sq, int piece_id) {
    if (persp == BLACK) {
        piece_side = 1 - piece_side;
        piece_sq = piece_sq ^ 56;
    }

    return piece_side*6*64 + piece_id*64 + piece_sq;
}

static ActiveIndices get_persp_indices(std::span<uint64_t> bbs, int persp) {
    ActiveIndices indices;
    indices.count = 0;

    for (int piece_side = 0; piece_side < 2; ++piece_side) {
        for (int piece = 0; piece < 6; ++piece) {
            uint64_t bb = bbs[piece_side*6+piece];

            for (int sq : set_bits(bb)) {
                indices.data[indices.count++] = get_feature(persp, piece_side, sq, piece);
            }
        }
    }

    return indices;
}

static void feed_l1(int16_t* RESTRICT a0, int* RESTRICT indices, int index_count) {
    // initialize with bias
    std::copy(nnue_b0, nnue_b0 + std::size(nnue_b0), a0);

    // add weights
    for (int ji = 0; ji < index_count; ++ji) {
        int j = indices[ji];

        for (size_t i = 0; i < NNUE_ACCUMULATOR_PERSP_SIZE; ++i) {
            a0[i] += int16_t(nnue_w0[j][i]);
        }
    }
}

static float forward_accumulator(int16_t* RESTRICT accumulator) {
    alignas(32) uint8_t a0[NNUE_ACCUMULATOR_PERSP_SIZE*2];

    for (size_t i = 0; i < std::size(a0); ++i) {
        a0[i] = scaled_crelu<int16_t, uint8_t, 255>(accumulator[i], int16_t(64));
    }

    alignas(32) uint8_t a1[std::size(nnue_b1)];

    for (size_t i = 0; i < std::size(a1); ++i) {
#if defined(__AVX2__) && true
        __m256i ones = _mm256_set1_epi16(1);
        __m256i sum  = _mm256_setzero_si256();  // int32 accumulator

        for (size_t j = 0; j < std::size(a0); j += 32) {
            __m256i act = _mm256_load_si256((const __m256i*)&a0[j]);      // uint8
            __m256i w   = _mm256_load_si256((const __m256i*)&nnue_w1[i][j]); // int8

            __m256i prod = _mm256_maddubs_epi16(act, w);  // uint8*int8 → int16 (saturating), 32 pairs
            __m256i wide = _mm256_madd_epi16(prod, ones); // int16*1 → int32, 16 pairs summed
            sum = _mm256_add_epi32(sum, wide);
        }

        __m128i lo  = _mm256_castsi256_si128(sum);
        __m128i hi  = _mm256_extracti128_si256(sum, 1);
        __m128i s   = _mm_add_epi32(lo, hi);
        s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(1,0,3,2)));
        s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(2,3,0,1)));
        int32_t value = _mm_cvtsi128_si32(s) + nnue_b1[i];
#elif defined(__ARM_FEATURE_MATMUL_INT8)
        int32x4_t sum = vdupq_n_s32(0);

        for (size_t j = 0; j < std::size(a0); j += 16) {
            uint8x16_t act = vld1q_u8(&a0[j]);
            int8x16_t  w   = vld1q_s8(&nnue_w1[i][j]);
            sum = vusdotq_s32(sum, act, w);
        }

        int32_t value = vaddvq_s32(sum) + nnue_b1[i];
#else
        int32_t value = nnue_b1[i];

        for (size_t j = 0; j < std::size(a0); ++j) {
            value += int16_t(nnue_w1[i][j]) * int16_t(a0[j]);
        }
#endif

        a1[i] = scaled_crelu<int32_t, uint8_t, 255>(value, 64*255);
    }

    int32_t out = nnue_b2[0];

    for (size_t j = 0; j < std::size(a1); ++j) {
        out += int16_t(nnue_w2[0][j]) * int16_t(a1[j]);
    }

    return scaled_sigmoid(out, 64*255);
}


float nnue_infer(std::span<uint64_t> bbs) {
    alignas(32) int16_t accumulator[2][NNUE_ACCUMULATOR_PERSP_SIZE];

    auto white_persp = get_persp_indices(bbs, WHITE);
    auto black_persp = get_persp_indices(bbs, BLACK);

    feed_l1(accumulator[0], white_persp.data, white_persp.count);
    feed_l1(accumulator[1], black_persp.data, black_persp.count);

    return forward_accumulator(accumulator[0]);
}

inline int32_t wdl_to_centipawns(float wdl) {
    wdl = std::clamp(wdl, 1e-7f, 1.0f - 1e-7f);
    int32_t centipawns = int32_t(400.0f * logf(wdl/(1.0f-wdl)));
    return centipawns;
}

int32_t Position::nnue_eval() const {
    auto bbs = to_bitboards();
    float wdl = nnue_infer(bbs);
    return wdl_to_centipawns(wdl);
}

#ifdef USE_NNUE
void Position::init_nnue_accumulator() {
    auto bbs = to_bitboards();

    auto white_persp = get_persp_indices(bbs, WHITE);
    auto black_persp = get_persp_indices(bbs, BLACK);

    feed_l1(acc().half(WHITE), white_persp.data, white_persp.count);
    feed_l1(acc().half(BLACK), black_persp.data, black_persp.count);
}
#endif

static const int piece_id_table[NUM_PIECE_TYPES] = {
    0xffffff,
    0,
    3,
    1,
    2,
    4,
    5,
}; 

template<int SIGN>
ALWAYS_INLINE void update_accumulator_feature(int16_t* RESTRICT accumulator_half, int feature) {
    for (size_t i = 0; i < NNUE_ACCUMULATOR_PERSP_SIZE; ++i) {
        accumulator_half[i] += SIGN * nnue_w0[feature][i];
    }
}

static void update_accumulator_persp(int16_t* RESTRICT accumulator_half, Piece captured_piece, int captured_pos, Piece moving_piece_start, Piece moving_piece_end, int move_from, int move_to, int rook_from, int rook_to, int moving_side, int sign, int persp) {
    // moving piece

    int feature_from = get_feature(persp, moving_side, move_from, piece_id_table[moving_piece_start]);
    int feature_to   = get_feature(persp, moving_side, move_to, piece_id_table[moving_piece_end]);

    if (sign == -1) {
        std::swap(feature_from, feature_to);
    }

    update_accumulator_feature<-1>(accumulator_half, feature_from);
    update_accumulator_feature< 1>(accumulator_half, feature_to);

    // captured piece

    if (captured_piece != PIECE_NONE) {
        int feature_cap = get_feature(persp, opponent(moving_side), captured_pos, piece_id_table[captured_piece]);

        if (sign != -1) {
            update_accumulator_feature<-1>(accumulator_half, feature_cap);
        }
        else {
            update_accumulator_feature< 1>(accumulator_half, feature_cap);
        }
    }

    // castling

    if (rook_from != rook_to) {
        int feature_rook_from = get_feature(persp, moving_side, rook_from, piece_id_table[PIECE_ROOK]);
        int feature_rook_to   = get_feature(persp, moving_side, rook_to, piece_id_table[PIECE_ROOK]);

        if (sign == -1) {
            std::swap(feature_rook_from, feature_rook_to);
        }

        update_accumulator_feature<-1>(accumulator_half, feature_rook_from);
        update_accumulator_feature< 1>(accumulator_half, feature_rook_to);
    }
}

#ifdef USE_NNUE
void Position::update_eval(Piece captured_piece, int captured_pos, Piece moving_piece_start, Piece moving_piece_end, int move_from, int move_to, int rook_from, int rook_to, int side, int sign) {
    update_accumulator_persp(acc().half(WHITE), captured_piece, captured_pos, moving_piece_start, moving_piece_end, move_from, move_to, rook_from, rook_to, side, sign, WHITE);
    update_accumulator_persp(acc().half(BLACK), captured_piece, captured_pos, moving_piece_start, moving_piece_end, move_from, move_to, rook_from, rook_to, side, sign, BLACK);
    incr_eval = wdl_to_centipawns(forward_accumulator(acc().ptr()));
}
#endif