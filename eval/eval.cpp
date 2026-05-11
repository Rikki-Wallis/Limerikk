#include "limerikk.h"

int main(int argc, const char** argv) {
    if (argc < 2) {
        print("Usage: {} <fen>\n", argv[0]);
        return 1;
    }

    const char* fen = argv[1];

    auto res = Position::parse_fen(fen);

    if (!res) {
        print("Invalid fen '{}'\n", fen);
        return 1;
    }

    Position pos = std::move(*res);

    auto bbs = pos.to_bitboards();
    float x = nnue_infer(bbs);
    print("{}\n", x);

    return 0;
}