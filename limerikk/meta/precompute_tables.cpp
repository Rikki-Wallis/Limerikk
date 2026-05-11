#include <cstdint>
#include <cstddef>
#include <bit>
#include <vector>
#include <cstdio>
#include <format>
#include <random>
#include <limits>
#include <memory.h>

typedef struct { uint64_t state;  uint64_t inc; } pcg32_random_t;

static uint32_t pcg32_random_r(pcg32_random_t* rng)
{
    uint64_t oldstate = rng->state;
    // Advance internal state
    rng->state = oldstate * 6364136223846793005ULL + (rng->inc|1);
    // Calculate output function (XSH RR), uses old state for max ILP
    uint32_t xorshifted = uint32_t(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << ((-(int32_t)rot) & 31));
}

static uint64_t rand64(pcg32_random_t* rng) {
    uint64_t a = pcg32_random_r(rng);
    uint64_t b = pcg32_random_r(rng);
    return (a << 32) | b;
}

size_t get_index(uint64_t perm, uint64_t magic, size_t shift) {
    return static_cast<size_t>((perm * magic) >> shift);
}

template<typename...Args>
inline int fprint(FILE* stream, const std::format_string<Args...>& fmt, Args&&...args) {
    std::string str = std::format(fmt, std::forward<Args>(args)...);
    return fprintf(stream,"%s", str.c_str());
}

inline int fprint(FILE* stream, const std::format_string<>& fmt) {
    std::string str = std::format(fmt);
    return fprintf(stream, "%s", str.c_str());
}

class Bitset {
public:
    Bitset(size_t size)
        : _data((size + 63)/64)
    {
    }

    void set(size_t index) {
        _data[index/64] |= (uint64_t)1 << (index%64);
    }

    bool get(size_t index) {
        return (_data[index/64] >> (index%64)) & 1;
    }

    void clear() {
        memset(_data.data(), 0, sizeof(uint64_t) * _data.size());
    }

private:
    std::vector<uint64_t> _data;
};

// find a magic number that multiplies a permutation of a mask, such that it produces a unique top n bits per permutation, where n is the number of bits in the mask
uint64_t find_magic_number(uint64_t mask, size_t bits) {
    size_t shift = 64 - bits;
    Bitset hit((size_t)1 << bits);

    pcg32_random_t rng = {
        .state = 59323,
        .inc = 3,
    };

    for(;;) {
        // get a sparse random number
        uint64_t magic = rand64(&rng) & rand64(&rng) & rand64(&rng);

        bool ok = true;
        uint64_t perm = mask;
        hit.clear();

        for(;;) {
            size_t index = get_index(perm, magic, shift);

            if (hit.get(index)) { // there is a collision
                ok = false;
                break;
            }

            hit.set(index);

            if (perm == 0) {
                break;
            }
        
            perm = (perm - 1) & mask;
        }

        if (ok) {
            return magic;
        }
    }
}

template<typename T>
static void dump_array(FILE* stream, const std::string& name, const char* type, const T* x, size_t count) {
    fprint(stream, "static const {} {}[] = {{\n", type, name);

    for (size_t i = 0; i < count; ++i) {
        if (i % 8 == 0) {
            fprint(stream, "    ");
        }

        fprint(stream, "0x{:x}, ", x[i]);

        if ((i+1) % 8 == 0) {
            fprint(stream, "\n");
        }
    }

    fprint(stream, "}};\n\n");
}

struct Tables {
    uint64_t              magic[64];
    size_t                shift[64];
    uint64_t              mask [64];
    std::vector<uint64_t> moves[64];

    void dump(FILE* stream, const char* name) const {
        dump_array(stream, std::format("{}_mask", name), "uint64_t", mask, std::size(mask));
        dump_array(stream, std::format("{}_magic", name), "uint64_t", magic, std::size(magic));
        dump_array(stream, std::format("{}_shift", name), "size_t", shift, std::size(shift));

        for (int i = 0; i < 64; ++i) {
            dump_array(stream, std::format("{}_move_buffer_{}", name, i), "uint64_t", moves[i].data(), moves[i].size());
        }

        fprint(stream, "static const uint64_t* {}_move[] = {{\n", name);
        for (int i = 0; i < 64; ++i) {
            fprint(stream, "    {}_move_buffer_{}, \n", name, i);
        }
        fprint(stream, "}};\n\n");
    }
};

static Tables generate_table(uint64_t(*mask_at)(size_t), uint64_t(*moves_at)(size_t,uint64_t)) {
    Tables table;

    for (size_t sq = 0; sq < 64; ++sq) {
        uint64_t mask = mask_at(sq);
        size_t bits = std::popcount(mask);
        size_t shift = 64 - bits;

        uint64_t magic = find_magic_number(mask, bits);

        table.moves[sq].resize(1ULL << bits);
        
        uint64_t perm = mask;
        
        for(;;) {
            size_t index = get_index(perm, magic, shift);

            table.moves[sq][index] = moves_at(sq, perm);

            if (perm == 0) {
                break;
            }

            perm = (perm - 1) & mask;
        }

        table.magic[sq] = magic;
        table.shift[sq] = shift;
        table.mask[sq]  = mask;
    }

    return table;
}

static uint64_t get_rank(size_t rank) {
    return (uint64_t)0xff << rank*8;
}

static uint64_t get_file(size_t file) {
    uint64_t ver_slice = (uint64_t)1 << file;
    uint64_t ver = 0;

    for(int i = 0; i < 8; ++i) {
        ver |= ver_slice << i*8;
    }

    return ver;
}

uint64_t rook_mask_at(size_t sq) {
    size_t file = sq % 8;
    size_t rank = sq / 8;

    uint64_t hor = get_rank(rank);
    uint64_t ver = get_file(file);

    // mask board edges
    hor &= ~(get_file(0) | get_file(7));
    ver &= ~(get_rank(0) | get_rank(7));

    uint64_t me = (uint64_t)1 << sq;

    return (hor | ver) & (~me);
}

uint64_t slide(size_t sq, uint64_t blockers, uint64_t(*translate)(uint64_t)) {
    uint64_t result = 0;

    uint64_t bb = (uint64_t)1 << sq;

    while (bb > 0) {
        bb = translate(bb);

        result |= bb;

        if (bb & blockers) {
            break;
        }
    }

    return result;
}

uint64_t bishop_moves_at(size_t sq, uint64_t blockers) {
    uint64_t result = 0;

    result |= slide(sq, blockers, [](uint64_t x){ return (x << 7) & (~get_file(7)); }); // left up
    result |= slide(sq, blockers, [](uint64_t x){ return (x << 9) & (~get_file(0)); }); // right up
    result |= slide(sq, blockers, [](uint64_t x){ return (x >> 7) & (~get_file(0)); }); // right down
    result |= slide(sq, blockers, [](uint64_t x){ return (x >> 9) & (~get_file(7)); }); // left down

    return result;
}

uint64_t bishop_mask_at(size_t sq) {
    uint64_t result = 0;

    result |= slide(sq, 0, [](uint64_t x){ return (x << 7) & (~get_file(7)); }) & (~(get_file(0) | get_rank(7))); // left up
    result |= slide(sq, 0, [](uint64_t x){ return (x << 9) & (~get_file(0)); }) & (~(get_file(7) | get_rank(7))); // right up
    result |= slide(sq, 0, [](uint64_t x){ return (x >> 7) & (~get_file(0)); }) & (~(get_file(7) | get_rank(0))); // right down
    result |= slide(sq, 0, [](uint64_t x){ return (x >> 9) & (~get_file(7)); }) & (~(get_file(0) | get_rank(0))); // left down

    return result;
}

uint64_t rook_moves_at(size_t sq, uint64_t blockers) {
    uint64_t result = 0;

    result |= slide(sq, blockers, [](uint64_t x){ return x << 8; }); // up
    result |= slide(sq, blockers, [](uint64_t x){ return x >> 8; }); // down
    result |= slide(sq, blockers, [](uint64_t x){ return (x << 1) & (~get_file(0)); }); // right
    result |= slide(sq, blockers, [](uint64_t x){ return (x >> 1) & (~get_file(7)); }); // left

    return result;
}

uint64_t knight_moves_at(int from) {
    uint64_t bb = (uint64_t)1 << from;

    uint64_t ul = (bb >> 17) & ~get_file(7);
    uint64_t ur = (bb >> 15) & ~get_file(0); 
    uint64_t lu = (bb >> 10) & ~(get_file(7) | get_file(6)); 
    uint64_t ru = (bb >> 6)  & ~(get_file(0) | get_file(1)); 
    uint64_t dl = (bb << 17) & ~get_file(0); 
    uint64_t dr = (bb << 15) & ~get_file(7); 
    uint64_t ld = (bb << 10) & ~(get_file(0) | get_file(1)); 
    uint64_t rd = (bb << 6)  & ~(get_file(7) | get_file(6)); 

    return ul | ur | lu | ru | dl | dr | ld | rd;
}

uint64_t king_moves_at(int from) {
    uint64_t bb = (uint64_t)1 << from;

    uint64_t l  = (bb >> 1) & (~get_file(7));
    uint64_t lu = (bb << 7) & (~get_file(7));
    uint64_t u  = (bb << 8);
    uint64_t ru = (bb << 9) & (~get_file(0));
    uint64_t r  = (bb << 1) & (~get_file(0));
    uint64_t rd = (bb >> 7) & (~get_file(0));
    uint64_t d  = (bb >> 8);
    uint64_t ld = (bb >> 9) & (~get_file(7));

    return l | lu | u | ru | r | rd | d | ld;
}

uint64_t white_pawn_attacks_at(int from) {
    uint64_t bb = (uint64_t)1 << from;
    uint64_t left  = (bb << 7) & (~get_file(7));
    uint64_t right = (bb << 9) & (~get_file(0));
    return left | right;
}

uint64_t black_pawn_attacks_at(int from) {
    uint64_t bb = (uint64_t)1 << from;
    uint64_t left  = (bb >> 9) & (~get_file(7));
    uint64_t right = (bb >> 7) & (~get_file(0));
    return left | right;
}

/*
uint64_t white_passed_pawn_mask(int from) {
    uint64_t pawn_bb = (uint64_t)1 << from;
    uint64_t left_file = bb_to_file(((single_pawn_bb << 1) & ~get_file(0)));
    uint64_t right_file = bb_to_file(((single_pawn_bb >> 1) & ~get_file(7)));
    uint64_t middle_file = bb_to_file(pawn_bb);

    uint64_t all_files = left_file | right_file | middle_file;

    uint64_t passed_mask = 0;

    for (int r = 7; r >= 0; --r) {
        uint64_t rank = get_rank(r);

        if (pawn_bb & rank) {
            break;
        } else {
            passed_mask |= (rank & all_files);
        }
    }

    return passed_mask;
}

uint64_t black_passed_pawn_mask(int from) {
    uint64_t pawn_bb = (uint64_t)1 << from;
    uint64_t left_file = bb_to_file(((single_pawn_bb >> 1) & ~FILE_H));
    uint64_t right_file = bb_to_file(((single_pawn_bb << 1) & ~FILE_A));
    uint64_t middle_file = bb_to_file(pawn_bb);

    uint64_t all_files = left_file | right_file | middle_file;
    uint64_t rank_search_order[8] = {RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8};
    uint64_t passed_mask = 0;

    for (auto rank : rank_search_order) {
        if (pawn_bb & rank) {
            break;
        } else {
            passed_mask |= (rank & all_files);
        }
    }

    return passed_mask;
}
*/

static void dump_trivial_move_table(FILE* file, const std::string& name, uint64_t(*moves_at)(int)) {
    fprint(file, "const uint64_t {}[] = {{\n", name);
    for (int i = 0; i < 64; ++i) {
        if (i%8==0) {
            fprint(file, "  ");
        }

        uint64_t val = moves_at(i);

        fprint(file, "0x{:x},", val);

        if ((i+1) % 8 == 0) {
            fprint(file, "\n");
        }
    }
    fprint(file, "}};\n\n");
}

inline int sign(int x) {
    if (x < 0) {
        return -1;
    }
    else if (x > 0) {
        return 1;
    }
    else {
        return 0;
    }
}

static uint64_t compute_line(int a, int b, bool bounded) {
    if (a == b) {
        return 0;
    }

    int ar = (a >> 3) & 7;
    int af = a & 7;

    int br = (b >> 3) & 7;
    int bf = b & 7;

    bool straight = ar == br || af == bf; // either they are on the same rank or the same file
    bool diag     = std::abs(br-ar) == std::abs(bf-af); // or the difference between the files is the same as the distance between the ranks

    if (!straight && !diag) {
        return 0;
    }

    // we'll slide from a to b

    int dr = sign(br-ar);
    int df = sign(bf-af);

    uint64_t result = 0;

    int r = ar;
    int f = af;

    for (;;) {
        r += dr;
        f += df;

        if (r < 0 || r >= 8 || f < 0 || f >= 8) {
            break;
        }

        int sq = r * 8 + f;

        if (bounded && sq == b) {
            break;
        }

        result |= uint64_t(1) << sq;
    }

    r = br;
    f = bf;

    for (;;) {
        r -= dr;
        f -= df;

        if (r < 0 || r >= 8 || f < 0 || f >= 8) {
            break;
        }

        int sq = r * 8 + f;

        if (bounded && sq == a) {
            break;
        }

        result |= uint64_t(1) << sq;
    }

    return result;
}

template<typename F>
static void generate_line_segment_table(FILE* file, const std::string& name, F&& func) {
    for (int a = 0; a < 64; ++a) {
        fprint(file, "const uint64_t {}_from_{}[] = {{\n", name, a);
        for (int b = 0; b < 64; ++b) {
            if (b%8==0) {
                fprint(file, "  ");
            }

            uint64_t val = std::forward<F>(func)(a, b);

            fprint(file, "0x{:x},", val);

            if ((b+1) % 8 == 0) {
                fprint(file, "\n");
            }
        }
        fprint(file, "}};\n\n");
    }

    fprint(file, "const uint64_t* {}[64] = {{\n", name);
    for (int a = 0; a < 64; ++a) {
        fprint(file, "  {}_from_{},\n", name, a);
    }
    fprint(file, "}};\n\n");

}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprint(stderr, "Usage %s <path>\n", argv[0]);
        return 1;
    }

    fprint(stderr, "Pre-computing tables...\n");

    Tables rooks = generate_table(rook_mask_at, rook_moves_at);
    Tables bishops = generate_table(bishop_mask_at, bishop_moves_at);

    const char* path = argv[1];
    FILE* file = fopen(path, "w");

    if (!file) {
        fprint(stderr, "Failed to write %s\n", path);
        return 1;
    }

    fprint(file, "#pragma once\n\n");
    fprint(file, "#include <cstdint>\n\n");

    rooks.dump(file, "rook");
    bishops.dump(file, "bishop");

    dump_trivial_move_table(file, "knight_move_table", knight_moves_at);
    dump_trivial_move_table(file, "king_move_table", king_moves_at);
    dump_trivial_move_table(file, "white_pawn_attacks_table", white_pawn_attacks_at);
    dump_trivial_move_table(file, "black_pawn_attacks_table", black_pawn_attacks_at);

    generate_line_segment_table(file, "between", [](int a, int b){
        return compute_line(a, b, true);
    });

    generate_line_segment_table(file, "line", [](int a, int b){
        return compute_line(a, b, false);
    });

    return 0;
}