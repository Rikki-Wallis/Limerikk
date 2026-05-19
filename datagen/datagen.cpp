#include <random>
#include <cstdio>
#include <thread>
#include <mutex>

#include "limerikk.h"

static thread_local std::mt19937 rng(std::random_device{}());

constexpr int NUM_ITERATIONS = 100;
constexpr int NUM_MATCHES = 10000;
constexpr int RANDOM_HALF_MOVES = 10;
constexpr int NODE_BUDGET = 5000;

static const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static std::mutex io_mutex;

struct BinpackGame {
    std::array<uint64_t, 12> start;
    std::array<uint64_t, 12> end;
    std::vector<uint32_t> sequence;
    int outcome;
};

static std::pair<GameResult, BinpackGame> run_match() {
    int n_random = std::uniform_int_distribution<int>(RANDOM_HALF_MOVES, RANDOM_HALF_MOVES+5)(rng);

    for (;;) {
        Position pos = *Position::parse_fen(START_FEN);

        BinpackGame bpg{};

        for (int hm = 0;;++hm) {
            std::optional<GameResult> gr = pos.game_result();

            if (gr.has_value()) {
                if (bpg.sequence.size() > 0) {
                    bpg.end = pos.to_bitboards();
                    bpg.outcome = gr->result;
                    return {*gr, bpg};
                }
                else {
                    break;
                }
            }

            if (hm < n_random) { // For the first n moves, play random moves, to diversify the position
                MoveList moves = pos.generate_moves();
                pos.filter_moves(moves);

                size_t move_idx = std::uniform_int_distribution<size_t>(0, moves.count - 1)(rng);
                Move mv = moves.data[move_idx];

                pos.make_move(mv);
            }
            else { // after that, we let the engine make the move
                if (bpg.sequence.size() == 0) {
                    bpg.start = pos.to_bitboards();
                }

                std::atomic<bool> should_stop = false;

                NodeBudgeter budgeter(NODE_BUDGET);

                int64_t score= 0;
                Move mv= pos.best_move(20, should_stop, &budgeter, false, &score);

                if (pos.to_move == BLACK) {
                    score *= -1;
                }

                bool not_mate = std::abs(score) < (MATE_SCORE - 1000);
                bool quiet = pos.is_quiescent();

                pos.make_move(mv);

                int flag = 0;

                switch (move_type(mv)) {
                    case MOVE_NORMAL: break;
                    case MOVE_DOUBLE_PUSH: break;
                    case MOVE_EN_PASSANT: break;
                    case MOVE_SHORT_CASTLE: break;
                    case MOVE_LONG_CASTLE: break;
                    case MOVE_PROMOTION: {
                        switch (move_end_piece(mv)) {
                            case PIECE_KNIGHT:
                                flag = 1;
                                break;
                            case PIECE_BISHOP:
                                flag = 2;
                                break;
                            case PIECE_ROOK:
                                flag = 3;
                                break;
                            case PIECE_QUEEN:
                                flag = 4;
                                break;
                            default:
                                assert(false);
                                break;
                        }
                    } break;
                }

                bool good = not_mate && quiet;
                int16_t enc_score = int16_t(score);
                uint32_t enc = uint32_t(move_from(mv) | (move_to(mv) << 6) | (flag << 12) | (good << 15)) | (uint32_t(enc_score) << 16);
                bpg.sequence.push_back(enc);
            }
        }
    }
}

int main() {
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        std::string filename = std::format("data_{:%Y%m%d_%H%M%S}.bin", std::chrono::system_clock::now());
        FILE* file = fopen(filename.c_str(), "wb");

        std::atomic<int> match_count = 0;

        int nthreads = std::thread::hardware_concurrency();

        std::vector<std::thread> threads;

        std::atomic<int64_t> result_total = 0;

        for (int t = 0; t < nthreads; ++t) {
            threads.push_back(std::thread([&result_total, &match_count, file, iter](){
                for (;;) {
                    int mid = match_count.fetch_add(1);

                    if (mid >= NUM_MATCHES) {
                        break;
                    }

                    auto [result, bpg] = run_match();

                    {
                        std::lock_guard g(io_mutex);

                        int8_t magic = 67;
                        fwrite(&magic, sizeof(magic), 1, file);
                        
                        int8_t outcome = int8_t(bpg.outcome);
                        fwrite(&outcome, sizeof(outcome), 1, file);

                        uint16_t seq_len = uint16_t(bpg.sequence.size());
                        fwrite(&seq_len, sizeof(seq_len), 1, file);

                        fwrite(bpg.start.data(), sizeof(bpg.start[0]), bpg.start.size(), file);
                        fwrite(bpg.end.data(), sizeof(bpg.end[0]), bpg.end.size(), file);

                        fwrite(bpg.sequence.data(), sizeof(bpg.sequence[0]), bpg.sequence.size(), file);
                    }

                    auto old_total = result_total.fetch_add(result.result);

                    const char* reason = "";

                    switch (result.reason) {
                        case GAME_RESULT_CHECKMATE:
                            if (result.result == 1)  {
                                reason = "White mates";
                            }
                            else{
                                reason = "Black mates";
                            }
                            break;

                        case GAME_RESULT_STALEMATE:
                            reason = "Stalemate";
                            break;
                        
                        case GAME_RESULT_50_MOVE_RULE:
                            reason = "50 move rule";
                            break;

                        case GAME_RESULT_3_FOLD_REPETITION:
                            reason = "Three-fold repetition";
                            break;
                    }

                    {
                        std::lock_guard guard(io_mutex);
                        print("Batch {}/{}: Game {}/{}: {} ({}) (ot: {})\n", iter+1, NUM_ITERATIONS, mid+1, NUM_MATCHES, result.result, reason, old_total);
                    }
                }
            }));
        }

        for (auto& t : threads) {
            t.join();
        }

        auto x = result_total.load();
        print("Result total: {}\n", x);

        fclose(file);
    }

    return 0;
}