#include <random>
#include <atomic>
#include <map>
#include <thread>
#include <mutex>
#include <fstream>

#include "balanced_openings.h"
#include "limerikk.h"

std::mt19937 rng(std::random_device{}());
std::uniform_int_distribution<int> twin_dist(0, 1);
std::uniform_int_distribution<size_t> opening_dist(0, std::size(openings)-1);

constexpr double time_limit_per_move = 0.1f; 
constexpr int ngames = 16;

struct Param {
    float value;
    float lo, hi;
};

float perturb_amount(const Param& param, int iteration) {
    float c = std::max((param.hi-param.lo) * 0.05f, 0.01f);
    return c / std::pow(float(iteration + 1), 0.101f);
}

// These are continuously updated but rounded to integers in most cases
struct Params {
    std::map<std::string, Param> params = {
        { "lmr_rate_base", { 0.0f, 0.0f, 3.0f }},
        { "lmr_rate_divisor", { 0.0f, 0.5f, 5.0f }},
        { "singular_margin_factor", { 0.0f, 0.5f, 5.0f }},
        { "rfp_margin_factor", { 0.0f, 10.0f, 1000.0f }},
        { "rfp_improving_bonus", { 0.0f, 0.0f, 1000.0f }},
        { "fp_margin_factor", { 0.0f, 10.0f, 1000.0f }},
        { "lmr_history_bonus_threshold", { 0.0f, 100.0f, 5000.0f }},
        { "history_bonus_factor", { 0.0f, 0.1f, 5.0f }},
        { "history_malus_factor", { 0.0f, 0.1f, 5.0f }},
        { "cont_history_bonus_factor", { 0.0f, 0.1f, 5.0f }},
        { "cont_history_malus_factor", { 0.0f, 0.1f, 5.0f }},
        { "qsearch_big_delta", { 0.0f, 400.0f, 2000.0f }},
        { "qsearch_delta_margin", { 0.0f, 50.0f, 1000.0f }},
        { "asp_initial_window_size", { 0.0f, 10.0f, 100.0f }},
        { "asp_window_growth_factor", { 0.0f, 1.1f, 100.0f }},
        { "nmp_r_base", { 0.0f, 1.0f, 6.0f }},
        { "nmp_r_divisor", { 0.0f, 1.0f, 12.0f }},
        { "lmp_index_base", { 0.0f, 1.0f, 5.0f }},
        { "lmp_index_factor", { 0.0f, 0.5f, 5.0f }},
    };

    void load_from_checkpoint(const char* path) {
        std::ifstream file(path);

        std::string line;
        while(std::getline(file, line)) {
            if (line.empty()) {
                continue;
            }

            std::istringstream iss(line);

            std::string attrib;
            float value;

            if (!(iss >> attrib >> value)) {
                print("Failed to parse checkpoint.\n");
                exit(1);
            }

            if (!params.contains(attrib)) {
                print("Unknown attribute {}\n", attrib);
                exit(1);
            }

            params.at(attrib).value = value;
        }
    }

    void dump() {
        print("Params:\n");
        for (auto& [name, p] : params) {
            print("  {}: {:.3f}\n", name, p.value);
        }
        print("\n");
    }

    SearchParameters convert() const {
        #define match(name) .name = params.at(#name).value
        #define match_int(name) .name = int(std::round(params.at(#name).value))

        return SearchParameters {
            match(lmr_rate_base), 
            match(lmr_rate_divisor), 
            match(singular_margin_factor), 
            match_int(rfp_margin_factor), 
            match_int(rfp_improving_bonus), 
            match_int(fp_margin_factor), 
            match_int(lmr_history_bonus_threshold), 
            match(history_bonus_factor), 
            match(history_malus_factor), 
            match(cont_history_bonus_factor), 
            match(cont_history_malus_factor), 
            match_int(qsearch_big_delta), 
            match_int(qsearch_delta_margin), 
            match_int(asp_initial_window_size), 
            match(asp_window_growth_factor), 
            match(nmp_r_base), 
            match(nmp_r_divisor), 
            match(lmp_index_base), 
            match(lmp_index_factor), 
        };

        #undef match
        #undef match_int
    }

    void clamp() {
        for (auto& [name, p] : params) {
            p.value = std::clamp(p.value, p.lo, p.hi);
        }
    }

    std::pair<Params, Params> perturb(int iteration) const {
        Params twins[2] = {
            *this, *this
        };

        for (auto& [name, p] : params) {
            int i = twin_dist(rng);
            float amount = perturb_amount(p, iteration);
            twins[i].params.at(name).value += amount;
            twins[(i+1)&1].params.at(name).value -= amount;
        }

        twins[0].clamp();
        twins[1].clamp();

        return {
            twins[0], twins[1]
        };
    }

    void update(const Params& p1, const Params& p2, float ak, float result) {
        for (auto& [name, p] : params)
        {
            float v1 = (p1.params.at(name).value - p.lo) / (p.hi - p.lo);
            float v2 = (p2.params.at(name).value - p.lo) / (p.hi - p.lo);
            float diff = v1 - v2;

            float normalized = (p.value - p.lo) / (p.hi - p.lo);
            normalized += ak * result / diff;
            p.value = p.lo + normalized * (p.hi - p.lo);
        }

        clamp();
    }
};

std::mutex print_mutex;

static int run_double_sided_game(size_t game_index, const char* opening, const SearchParameters& p1, const SearchParameters& p2) {
    (void)game_index;

    SearchParameters sides[2] = {
        p1,
        p2
    };

    int aggregate = 0;

    for (int round = 0; round < 2; ++round) { // play two rounds, switching sides with the opening
        Position pos = *Position::parse_fen(opening);

        std::optional<GameResult> result = std::nullopt;

        for (;;) {
            result = pos.game_result();

            if (result.has_value()) { // game over
                break;
            }

            TimeBudgeter budgeter(time_limit_per_move);

            std::atomic<bool> should_stop = false;
            Move move = pos.best_move(20, should_stop, &budgeter, sides[pos.to_move]);
            pos.make_move(move);
        }

        const char* reason = "";

        switch (result->reason) {
            case GAME_RESULT_CHECKMATE:
                if (result->result == 1)  {
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
            std::lock_guard<std::mutex> lock(print_mutex);
            print("Game {} round {}: {} ({})\n", game_index, round+1, result->result, reason);
        }

        if (round == 0) {
            aggregate += result->result;
        }
        else {
            aggregate -= result->result;
        }

        std::swap(sides[0], sides[1]);
    }

    return aggregate;
}

int main() {
    Params params{};
    params.load_from_checkpoint("params_checkpoint.txt");

    std::vector<Params> history{params};

    int nthreads = std::max((unsigned int)1, std::thread::hardware_concurrency());
    print("Running with {} threads.\n", nthreads);

    for (int iteration = 0; iteration < 1000; ++iteration) {
        
        std::vector<const char*> games;

        for (int i = 0; i < ngames; ++i) {
            const char* opening = openings[opening_dist(rng)];
            games.push_back(opening);
        }

        float ak = 0.005f / std::pow(float(iteration + 10), 0.3f);

        auto [p1, p2] = params.perturb(iteration);

        auto sp1 = p1.convert();
        auto sp2 = p2.convert();

        std::vector<std::thread> threads;
        threads.reserve(nthreads);

        std::atomic<size_t> opening_index{0};
        std::vector<int> thread_results(nthreads, 0);

        for (int t = 0; t < nthreads; ++t) {
            threads.emplace_back([&, t](){
                int local_sum = 0;

                while (true) {
                    size_t i = opening_index.fetch_add(1);

                    if (i >= games.size()) {
                        break;
                    }

                    local_sum += run_double_sided_game(i, games[i], sp1, sp2);
                }

                thread_results[t] = local_sum;
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        int result_aggregate = 0;

        for (auto x : thread_results) {
            result_aggregate += x;
        }

        float avg_result = float(result_aggregate) / float(games.size()*2);

        params.update(p1, p2, ak, avg_result);

        print("Iteration: {}\n  ak: {:.4f}\n  result: {}\n", iteration, ak, avg_result);

        params.dump();
        history.push_back(params);

        if (iteration % 10 == 0) {
            {
                std::ofstream f("params_checkpoint.txt");
                for (auto& [name, p] : params.params)
                    f << name << " " << p.value << "\n";
            }

            {
                std::vector<std::string> cols;

                std::ofstream f("trend.csv");
                int i = 0;

                for (auto& [name, _p] : params.params) {
                    if (i++ > 0) {
                        f << ", ";
                    }

                    f << name;
                    cols.push_back(name);
                }

                f << "\n";

                for (auto& entry : history) {
                    i = 0;
                    for (auto& name : cols) {
                        auto& p = entry.params.at(name);

                        if (i++ > 0) {
                            f << ", ";
                        }
                        f << p.value;
                    }
                    f << "\n";
                }
            }
        }
    }
}