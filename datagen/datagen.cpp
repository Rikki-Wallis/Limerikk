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

struct Record {
    std::array<uint64_t, 12> bbs;
    int32_t score;
    int8_t max_ply;
};

static GameResult run_match(FILE* file) {
    Position pos = *Position::parse_fen(START_FEN);

    std::vector<Record> records;

    std::optional<GameResult> gr = std::nullopt;

    int n_random = std::uniform_int_distribution<int>(RANDOM_HALF_MOVES, RANDOM_HALF_MOVES+5)(rng);

    for (int hm = 0;;++hm) {
        gr = pos.game_result();

        if (gr.has_value()) {
            break;
        }

        if (hm < n_random) { // For the first n moves, play random moves, to diversify the position
            MoveList moves = pos.generate_moves();
            pos.filter_moves(moves);

            size_t move_idx = std::uniform_int_distribution<size_t>(0, moves.count - 1)(rng);
            Move mv = moves.data[move_idx];

            pos.make_move(mv);
        }
        else { // after that, we let the engine make the move

            std::atomic<bool> should_stop = false;

            NodeBudgeter budgeter(NODE_BUDGET);

            int64_t score= 0;
            Move mv= pos.best_move(20, should_stop, &budgeter, {}, false, &score);

            if (pos.to_move == BLACK) {
                score *= -1;
            }

            bool not_mate = std::abs(score) < (MATE_SCORE - 1000);
            bool quiet = pos.is_quiescent();

            if (quiet && not_mate) {
                Record r;
                r.score = int32_t(score);
                r.bbs = pos.to_bitboards();
                r.max_ply = int8_t(pos.max_ply);
                records.push_back(r);
            }

            pos.make_move(mv);
        }
    }

    int8_t outcome = int8_t(gr->result);

    {
        std::lock_guard g(io_mutex);
        for (Record& r : records) {
            fwrite(r.bbs.data(), r.bbs.size() * sizeof(uint64_t), 1, file);
            fwrite(&r.score, sizeof(r.score), 1, file);
            fwrite(&r.max_ply, sizeof(r.max_ply), 1, file);
            fwrite(&outcome, sizeof(outcome), 1, file);
        }
    }

    return *gr;
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

                    GameResult result = run_match(file);

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