#include <thread>
#include <iostream>
#include <atomic>

#include "limerikk.h"

static const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static std::optional<Move> parse_uci_move(Position* pos, const std::string& move) {
    int from_f = move[0] - 'a';
    int from_r = move[1] - '1';
    int to_f   = move[2] - 'a';
    int to_r   = move[3] - '1';

    #define ASSERT_COORD_RANGE(coord) \
        if (coord < 0 || coord > 7) { \
            return std::nullopt; \
        } \

    ASSERT_COORD_RANGE(from_f);
    ASSERT_COORD_RANGE(from_r);
    ASSERT_COORD_RANGE(to_f);
    ASSERT_COORD_RANGE(to_r);

    int from = from_r * 8 + from_f;
    int to   = to_r * 8 + to_f;

    Piece start_piece = Piece(pos->piece_at[from]);
    Piece end_piece = start_piece;

    if (move.size() > 4) {
        switch (move[4]) {
            case 'q':
                end_piece = PIECE_QUEEN;
                break;
            case 'r':
                end_piece = PIECE_ROOK;
                break;
            case 'b':
                end_piece = PIECE_BISHOP;
                break;
            case 'n':
                end_piece = PIECE_KNIGHT;
                break;
            default:
                return std::nullopt;
        }
    }

    MoveType move_type = MOVE_NORMAL;

    if (end_piece != start_piece) {
        move_type = MOVE_PROMOTION;
    }

    if (start_piece == PIECE_KING) {
        if (move[0] == 'e' && move[2] == 'g') {
            move_type = MOVE_SHORT_CASTLE;
        }

        if (move[0] == 'e' && move[2] == 'c') {
            move_type = MOVE_LONG_CASTLE;
        }
    }

    if (start_piece == PIECE_PAWN) {
        if (std::abs(to_r-from_r) > 1) {
            move_type = MOVE_DOUBLE_PUSH;
        }

        if (pos->piece_at[to] == PIECE_NONE && to_f != from_f) {
            move_type = MOVE_EN_PASSANT;
        }
    }

    Piece captured_piece = Piece(pos->piece_at[get_captured_square(to, move_type, pos->to_move)]);

    return encode_move(from, to, move_type, end_piece, pos->to_move, captured_piece);
}

static void parse_position(const std::string& line, Position* pos) {
    const char* moves_string = " moves ";
    size_t moves_start = line.find(moves_string);
    std::string pos_part = line.substr(0, moves_start);
    std::string moves_part = (moves_start != std::string::npos) ? line.substr(moves_start + strlen(moves_string)) : "";

    if (pos_part.find("startpos") != std::string::npos) {
        *pos = *Position::parse_fen(START_FEN);
    }
    else if (pos_part.find("fen") != std::string::npos) {
        std::string fen = pos_part.substr(pos_part.find("fen") + 4);
        auto pos_result = Position::parse_fen(fen);
        if (!pos_result.has_value()) {
            std::cout << "Invalid FEN " << fen << "\n";
            return;
        }
        *pos = std::move(*pos_result);
    }
    else {
        std::cout << "Unrecognized position type\n";
        return;
    }

    // parse moves

    std::istringstream ss(moves_part);
    std::string move;

    while (ss >> move) {
        std::optional<Move> mv_result = parse_uci_move(pos, move);

        if (!mv_result.has_value()) {
            std::cout << "Illegal move " << move << "\n";
            break;
        }

        Move mv = *mv_result;

        if (!pos->is_move_legal_slow(mv)) {
            std::cout << "Illegal move " << move << "\n";
            break;
        }
        pos->make_move(mv);
    }
}

struct GoParams {
    std::optional<int> wtime;
    std::optional<int> btime;
    std::optional<int> winc;
    std::optional<int> binc;
    std::optional<int> movestogo;
    std::optional<int> movetime;
    std::optional<int> depth;
    std::optional<int> nodes;
    std::optional<int> mate;
    bool infinite;
    bool ponder;
};

static GoParams parse_go_command(const std::string& line) {
    std::istringstream ss(line);
    std::string token;

    GoParams g = {};

    while (ss >> token) {
        if      (token == "infinite") { g.infinite = true; }
        else if (token == "ponder") { g.ponder = true; }
        else if (token == "go") {}
        else {
            int value;
            if (!(ss >> value)) continue; // malformed command

            if      (token == "wtime")     { g.wtime = value; } 
            else if (token == "btime")     { g.btime = value; } 
            else if (token == "winc")      {  g.winc = value; } 
            else if (token == "binc")      {  g.binc = value; } 
            else if (token == "movestogo") { g.movestogo = value; } 
            else if (token == "movetime")  { g.movetime = value; } 
            else if (token == "depth")     { g.depth = value; } 
            else if (token == "nodes")     { g.nodes = value; } 
            else if (token == "mate")      { g.mate = value; } 
        }
    }

    return g;
}

static int allocate_time(int time, int inc) {
    return time/40 + inc/2;
}

static int calculate_move_time(const GoParams& p, int to_move) {
    if (p.infinite) {
        return INT32_MAX;
    }

    if (p.movetime) {
        return *p.movetime;
    }

    int time = to_move == WHITE ? p.wtime.value_or(0) : p.btime.value_or(0);
    int inc = to_move == WHITE ? p.winc.value_or(0) : p.binc.value_or(0);

    if (time == 0) {
        return INT32_MAX;
    }

    return allocate_time(time, inc);
}

class UCIBudgeter : public Budgeter {
public:
    UCIBudgeter(int node_count, double seconds)
        : _nodes(node_count), _seconds(seconds)
    {}

    virtual void init() override {
        _start = Clock::now();
    }

    virtual bool should_exit(int node_count) const override {
        int64_t elapsed_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - _start).count();
        double elapsed_s = double(elapsed_microseconds)/1000000.0;
        return node_count >= _nodes || elapsed_s >= _seconds;
    }

private:
    int _nodes;
    double _seconds;
    TimePoint _start;
};

int main() {
    std::ios::sync_with_stdio(false);
    std::cout.setf(std::ios::unitbuf);

    std::string line;
    std::thread thread;
    std::atomic<bool> should_stop = false;

    Position position = *Position::parse_fen(START_FEN);
    
    while (std::getline(std::cin, line)) {
        if (line == "uci") {
            std::cout << "id name limerikk\n";
            std::cout << "id author Jeremy and Rikki\n";
            std::cout << "uciok\n";
        }
        else if (line == "isready") {
            std::cout << "readyok\n";
        }
        else if (line == "ucinewgame") {
            position = *Position::parse_fen(START_FEN);
        }
        else if (line.starts_with("position")) {
            parse_position(line, &position);
        }
        else if (line == "quit") {
            should_stop = true;
            if (thread.joinable()) {
                thread.join();
            }
            return 0;
        }
        else if (line.starts_with("go")) {
            should_stop = true;
            if (thread.joinable()) {
                thread.join();
            }

            GoParams g = parse_go_command(line);

            int time_ms = calculate_move_time(g, position.to_move);
            double time_s = double(time_ms)/1000.0*0.95;

            int node_budget = g.nodes.value_or(INT_MAX);
            int depth = g.depth.value_or(40);

            should_stop = false;
            
            thread = std::thread([&position, depth, &should_stop, time_s, node_budget](){
                UCIBudgeter budgeter(node_budget, time_s);
                Move move = position.think(depth, should_stop, &budgeter, true);
                std::cout << "bestmove " << to_uci_move(move) << "\n";
            });
        }
        else if (line == "stop") {
            should_stop = true;
            if (thread.joinable()) {
                thread.join();
            }
        }
        else {
            std::cout << "Unrecognized command" << line << "\n";
        }
    }

    return 1;
}