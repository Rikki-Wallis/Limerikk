#pragma once

#include <optional>
#include <unordered_map>
#include <array>
#include <chrono>
#include <atomic>
#include <vector>
#include <fstream>
#include <iostream>
#include <cstdio>

#include "common.h"

#define USE_NNUE

constexpr size_t _ACCUMULATOR_PERSP_SIZE = 64;

constexpr size_t TT_SIZE = (1 << 20);
constexpr uint64_t TT_MASK = TT_SIZE - 1;

float nnue_infer(std::span<uint64_t> bbs);

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

#define ZOBRIST_INCLUDE_PIECES
#define ZOBRIST_INCLUDE_FLAGS
#define ZOBRIST_INCLUDE_EN_PASSANT_SQ
#define ZOBRIST_INCLUDE_SIDE

#define MAX_DEPTH 255

constexpr int32_t INF_SCORE  = 400000000;
constexpr int32_t MATE_SCORE = 32000; // just shy of int16 bounds

static constexpr uint64_t RANK_1 = 0x00000000000000ff;
static constexpr uint64_t RANK_2 = 0x000000000000ff00;
static constexpr uint64_t RANK_3 = 0x0000000000ff0000;
static constexpr uint64_t RANK_4 = 0x00000000ff000000;
static constexpr uint64_t RANK_5 = 0x000000ff00000000;
static constexpr uint64_t RANK_6 = 0x0000ff0000000000;
static constexpr uint64_t RANK_7 = 0x00ff000000000000;
static constexpr uint64_t RANK_8 = 0xff00000000000000;

static constexpr uint64_t FILE_A = 0x0101010101010101;
static constexpr uint64_t FILE_B = 0x0202020202020202;
static constexpr uint64_t FILE_C = 0x0404040404040404;
static constexpr uint64_t FILE_D = 0x0808080808080808;
static constexpr uint64_t FILE_E = 0x1010101010101010;
static constexpr uint64_t FILE_F = 0x2020202020202020;
static constexpr uint64_t FILE_G = 0x4040404040404040;
static constexpr uint64_t FILE_H = 0x8080808080808080;

static constexpr uint64_t WHITE_SHORT_SPACING = (FILE_F | FILE_G) & RANK_1;
static constexpr uint64_t WHITE_LONG_SPACING  = (FILE_B | FILE_C | FILE_D) & RANK_1;
static constexpr uint64_t BLACK_SHORT_SPACING = (FILE_F | FILE_G) & RANK_8;
static constexpr uint64_t BLACK_LONG_SPACING  = (FILE_B | FILE_C | FILE_D) & RANK_8;

enum Colour : uint8_t {
    WHITE,
    BLACK
};

enum Piece { // DO NOT CHANGE THE ORDER OF THIS
    PIECE_NONE,
    PIECE_PAWN,
    PIECE_ROOK,
    PIECE_KNIGHT,
    PIECE_BISHOP,
    PIECE_QUEEN,
    PIECE_KING,
    NUM_PIECE_TYPES
};

enum MoveType {
    MOVE_NORMAL,
    MOVE_DOUBLE_PUSH,
    MOVE_EN_PASSANT,
    MOVE_SHORT_CASTLE,
    MOVE_LONG_CASTLE,
    MOVE_PROMOTION,
};

enum PositionFlags {
    POSITION_FLAG_NONE = 0,
    POSITION_FLAG_WHITE_QCASTLE = (1 << 0),
    POSITION_FLAG_WHITE_KCASTLE = (1 << 1),
    POSITION_FLAG_BLACK_QCASTLE = (1 << 2),
    POSITION_FLAG_BLACK_KCASTLE = (1 << 3),
};

static const char* piece_alg_table[NUM_PIECE_TYPES] = {
    "uninitialized",
    "",
    "R",
    "N",
    "B",
    "Q",
    "K",
};

static const int32_t piece_value_table[NUM_PIECE_TYPES] = {
    0,
    100,
    500,
    300,
    300,
    900,
    0
};

using Move = uint32_t;

static constexpr Move NULL_MOVE = 0;

struct Side {
    uint64_t bb[NUM_PIECE_TYPES];

    uint64_t all() const;

    int64_t material_value() const;

    std::vector<uint64_t> get_bbs() const; 
};

static constexpr int NULL_SQUARE = -1;

struct Undo {
    uint32_t flags;
    Move move;
    int en_passant_sq;
    int32_t incremental_eval;
    uint64_t zobrist;
    bool is_checked[2];
    int half_move_clock;
};

struct ZobristTable {
    uint64_t side;
    std::array<uint64_t, 64> piece[2][NUM_PIECE_TYPES];
    uint64_t flags[255];
    uint64_t ep_table[65];

    uint64_t ep(int sq) const {
        return ep_table[sq + 1];
    }
};

extern const ZobristTable zobrist_table;

struct EvalParameters {
    // Material
    int piece_values[NUM_PIECE_TYPES];

    // PSTs
    int mg_pst[NUM_PIECE_TYPES][64];
    int eg_pst[NUM_PIECE_TYPES][64];

    // King Safety
    int q_castling_bonus;
    int q_castling_penalty;
    int k_castling_bonus;
    int k_castling_penalty;
    int shelter_strength_bonus;
    int shelter_strength_penalty;
    int open_file_bonus;
    int open_file_penalty;

    // Imbalance
    int bishop_pair_bonus;

    // Pawn Structure
    int isloated_pawn_penalty;
    int doubled_pawn_penalty;
    int connected_pawn_bonus;
    int passed_pawn_bonus;
};

// Pack as exactly 16 bytes
#pragma pack(push, 1)
struct PolyglotEntry {
    uint64_t key;
    uint16_t move;
    uint16_t weight;
    uint32_t learn;
};
#pragma pack(pop)

struct MoveList {
    Move data[256];
    int count;

    struct Iterator {
        Move* ptr;

        bool operator!=(const Iterator& other) const {
            return ptr != other.ptr;
        }

        Iterator& operator++() {
            ptr++;
            return *this;
        }

        Move& operator*() {
            return *ptr;
        }
    };

    Iterator begin() { return { .ptr = data }; }
    Iterator end() { return { .ptr = data + count }; }
};

enum GameResultReason {
    GAME_RESULT_CHECKMATE,
    GAME_RESULT_STALEMATE,
    GAME_RESULT_3_FOLD_REPETITION,
    GAME_RESULT_50_MOVE_RULE
};

struct GameResult {
    int result;
    GameResultReason reason;
};

class Budgeter {
public:
    virtual ~Budgeter() = default;
    virtual void init() {}
    virtual bool should_exit(int node_count) const = 0;
    virtual bool should_start_next_iteration() const = 0;
};

class NullBudgeter : public Budgeter {
public:
    virtual bool should_exit(int node_count) const override;
    virtual bool should_start_next_iteration() const override;
};

extern NullBudgeter null_budgeter;

struct Accumulator {
    alignas(32) int16_t data[2][_ACCUMULATOR_PERSP_SIZE];

    int16_t* half(int persp) { return data[persp]; }
    int16_t* ptr() { return data[0]; }
};

struct SearchStatistics {
    int nodes;
    int qnodes;
    float time;
    int sel_depth;
    float mean_cutoff_index;
};

struct SearchEntry {
    Move move;
};

struct SearchMetrics {
    int node_count = 0;
    int qnode_count = 0;

    int beta_cutoff_index_sum = 0;
    int beta_cutoff_count = 0;

    int sel_depth = 0;
};

struct SearchContext {
    SearchMetrics* metrics;
    bool exited = false;

    Budgeter* budgeter;
    std::atomic<bool> should_stop = false;

    SearchContext(Budgeter* budgeter)
        : budgeter(budgeter)
    {
    }

    bool exit_on_node();
};


struct Position {
    Side sides[2];
    int to_move;
    int en_passant_sq;
    uint8_t piece_at[64];
    uint32_t flags;

    int half_move_clock;

    bool is_checked[2];

    uint64_t zobrist;

    std::vector<Undo> undo_stack;

#ifdef USE_NNUE
    std::vector<Accumulator> accumulator_stack;
#endif
    int32_t incr_eval;

    Position()
        : to_move(WHITE), en_passant_sq(NULL_SQUARE), flags(0), half_move_clock(0)
    {
        memset(sides, 0, sizeof(sides));
        memset(piece_at, 0, sizeof(piece_at));
        zobrist = compute_zobrist();
        #ifdef USE_NNUE
        accumulator_stack.push_back({});
        #endif
    }

    void display(bool display_metadata=false) const;
    static std::optional<Position> parse_fen(const std::string& fen);
    std::string fen() const;

    std::array<uint64_t, 12> to_bitboards() const;

    MoveList generate_moves() const;
    MoveList generate_captures() const;

    std::unordered_map<std::string, Move> name_moves(std::span<Move> moves);

    int32_t non_pawn_material() const;

    uint64_t all_pieces() const;

    void make_move(Move move);
    void unmake_move();

    void make_null_move();
    void unmake_null_move();

    std::vector<Side> get_sides() const;  
    
    int get_king_sq(int side) const;
    uint64_t generate_pin_mask(int side) const;

    bool is_king_square_attacked(int side, int square) const;

    int lowest_value_attacker(int sq, int side, uint64_t occupancy) const;

    void filter_moves(MoveList& moves);

    void verify_integrity() const;

    int32_t compute_eval() const;
    int32_t nnue_eval() const;

    int32_t signed_eval();

    // @note if no castle, make rook_from == rook_ro
    void update_eval(Piece captured_piece, int captured_pos, Piece moving_piece_start, Piece moving_piece_end, int move_from, int move_to, int rook_from, int rook_to, int side, int sign=1);
    void update_is_checked();

    Move best_move(SearchContext& s, int depth, bool enable_uci_info=false, int64_t* score_out=nullptr, SearchStatistics* stats = nullptr);
    
    bool is_move_legal_slow(Move move);

    std::optional<GameResult> game_result();
    Move think(SearchContext& s, int depth, bool enable_uci_info=false, int64_t* score_out=nullptr);

    uint64_t compute_zobrist() const;

    void update_en_passant_sq(int sq);

    bool is_quiescent();

    // eval
    int64_t pawn_structure(int colour, uint64_t ally_pawn_bb) const;
    int64_t king_safety(int colour, uint64_t king_bb, uint64_t pawn_bb) const;
    int64_t bishop_imbalance() const;

    bool is_threefold_repetition() const;
    int64_t mobility(int colour) const;

    #ifdef USE_NNUE
    void init_nnue_accumulator();
    inline Accumulator& acc() {
        return accumulator_stack.back();
    }
    #endif

    // polygot encoding & decoding
    uint64_t encode_polyglot();
    Move decode_polyglot(PolyglotEntry move);
};

int get_captured_square(int to, MoveType ty, int side);

int32_t mg_unsigned_pst_value(Piece piece, int square, int side);
int32_t eg_unsigned_pst_value(Piece piece, int square, int side);

inline std::pair<char, int> square_alg(size_t sq) {
    char file = sq % 8 + 'a';
    int rank  = int(sq / 8 + 1);

    return {file, rank};
}

inline int opponent(int side) {
    return (side + 1) & 1;
}

inline uint64_t sq_to_bb(size_t index) {
    return (uint64_t)1 << index;
}

uint64_t perft_search(int depth, Position& position);

inline int move_from(Move move) {
    return move & 0b111111;
}

inline int move_to(Move move) {
    return (move >> 6) & 0b111111;
}

inline MoveType move_type(Move move) {
    int type = (move >> 12) & 0b111;
    return (MoveType)type;
}

inline Piece move_end_piece(Move move) {
    int piece = (move >> 15) & 0b111;
    return (Piece)piece;
}

inline int move_side(Move move) {
    int side = (move >> 18) & 1;
    return side;
}

inline Piece move_captured_piece(Move move) {
    return Piece((move >> 19) & 0b111);
}

inline bool is_capture(Move move) {
    return move_captured_piece(move) != PIECE_NONE;
}

inline int move_captured_square(Move move) {
    return get_captured_square(move_to(move), move_type(move), move_side(move));
}

inline Move encode_move(int from, int to, MoveType type, Piece end_piece, int side, Piece captured) {
    Move mv = (uint32_t)from & 0b111111; 
    mv     |= ((uint32_t)to & 0b111111) << 6;
    mv     |= ((uint32_t)type & 0b111) << 12;
    mv     |= ((uint32_t)end_piece & 0b111) << 15;
    mv     |= ((uint32_t)side & 1) << 18;
    mv     |= ((uint32_t)captured & 0b111) << 19;
    return mv;
}

inline uint64_t bb_to_file(uint64_t bb) {
    static const uint64_t file_table[8] = { FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H };
    if (bb == 0) return 0;
    int sq = std::countr_zero(bb);
    int file = sq & 7; 
    return file_table[file];
}

uint64_t knight_moves(int from, uint64_t allies);
uint64_t rook_moves(int from, uint64_t all_pieces, uint64_t allies);
uint64_t bishop_moves(int from, uint64_t all_pieces, uint64_t allies);
uint64_t queen_moves(int from, uint64_t all_pieces, uint64_t allies);

// Polygot
PolyglotEntry read_entry(uint64_t index);
uint64_t find_first(uint64_t key, uint64_t num_entries);
std::vector<PolyglotEntry> probe_book(uint64_t key);
PolyglotEntry choose_move(const std::vector<PolyglotEntry>& moves);


template<typename T>
static T bool_to_mask(bool x) {
    return static_cast<T>(-static_cast<int>(x));
}

std::string to_uci_move(Move move);

class TimeBudgeter : public Budgeter {
public:
    TimeBudgeter(double limit_seconds)
        : _limit(limit_seconds)
    {}

    virtual void init() override {
        _start = Clock::now();
    }

    virtual bool should_exit(int node_count) const override {
        (void)node_count;
        int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - _start).count();
        double s = double(microseconds)/1000000.0;
        return s >= _limit;
    }

    virtual bool should_start_next_iteration() const override {
        return true;
    }

private:
    double _limit;
    TimePoint _start;
};

class NodeBudgeter : public Budgeter {
public:
    NodeBudgeter(int count)
        : _limit(count)
    {}

    virtual bool should_exit(int node_count) const override {
        return node_count >= _limit;
    }

    virtual bool should_start_next_iteration() const override {
        return true;
    }

private:
    int _limit;
};