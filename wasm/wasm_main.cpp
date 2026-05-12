#include <emscripten/emscripten.h>
#include <string>
#include <sstream>
#include <optional>
#include <iostream>
#include <atomic>

#include "limerikk.h"

static const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Output queue — JS polls this.
static std::string g_output_buf;

static void engine_write(const std::string& line) {
    g_output_buf += line + "\n";
}

// Streambuf that routes std::cout into engine_write.
class CapturingBuf : public std::streambuf {
    std::string line;
protected:
    int overflow(int c) override {
        if (c == '\n') { engine_write(line); line.clear(); }
        else if (c != EOF) line += static_cast<char>(c);
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        for (std::streamsize i = 0; i < n; ++i) overflow((unsigned char)s[i]);
        return n;
    }
};

// ---- UCI helpers (same logic as uci.cpp) ----

static std::optional<Move> parse_uci_move(Position* pos, const std::string& move) {
    if (move.size() < 4) return std::nullopt;
    int from_f = move[0]-'a', from_r = move[1]-'1';
    int to_f   = move[2]-'a', to_r   = move[3]-'1';
    auto ok = [](int v){ return v >= 0 && v <= 7; };
    if (!ok(from_f)||!ok(from_r)||!ok(to_f)||!ok(to_r)) return std::nullopt;
    int from = from_r*8+from_f, to = to_r*8+to_f;
    Piece sp = Piece(pos->piece_at[from]), ep = sp;
    if (move.size() > 4) {
        switch(move[4]) {
            case 'q': ep=PIECE_QUEEN;  break; case 'r': ep=PIECE_ROOK;   break;
            case 'b': ep=PIECE_BISHOP; break; case 'n': ep=PIECE_KNIGHT; break;
            default: return std::nullopt;
        }
    }
    MoveType mt = MOVE_NORMAL;
    if (ep != sp) mt = MOVE_PROMOTION;
    if (sp == PIECE_KING) {
        if (move[0]=='e'&&move[2]=='g') mt=MOVE_SHORT_CASTLE;
        if (move[0]=='e'&&move[2]=='c') mt=MOVE_LONG_CASTLE;
    }
    if (sp == PIECE_PAWN) {
        if (std::abs(to_r-from_r)>1) mt=MOVE_DOUBLE_PUSH;
        if (pos->piece_at[to]==PIECE_NONE && to_f!=from_f) mt=MOVE_EN_PASSANT;
    }
    Piece cap = Piece(pos->piece_at[get_captured_square(to, mt, pos->to_move)]);
    return encode_move(from, to, mt, ep, pos->to_move, cap);
}

static void parse_position(const std::string& line, Position* pos) {
    const char* ms = " moves ";
    size_t mi = line.find(ms);
    std::string pp = line.substr(0, mi);
    std::string mp = (mi != std::string::npos) ? line.substr(mi+strlen(ms)) : "";
    if (pp.find("startpos") != std::string::npos) {
        *pos = *Position::parse_fen(START_FEN);
    } else if (pp.find("fen") != std::string::npos) {
        auto r = Position::parse_fen(pp.substr(pp.find("fen")+4));
        if (!r) { std::cout<<"Invalid FEN\n"; return; }
        *pos = std::move(*r);
    }
    std::istringstream ss(mp); std::string mv;
    while (ss >> mv) {
        auto r = parse_uci_move(pos, mv);
        if (!r||!pos->is_move_legal_slow(*r)) { std::cout<<"Illegal move "<<mv<<"\n"; break; }
        pos->make_move(*r);
    }
}

struct GoParams {
    std::optional<int> wtime,btime,winc,binc,movetime,depth,nodes;
    bool infinite=false;
};

static GoParams parse_go(const std::string& line) {
    std::istringstream ss(line); std::string t; GoParams g{};
    while (ss>>t) {
        if (t=="infinite"){g.infinite=true;continue;}
        if (t=="go"||t=="ponder") continue;
        int v; if(!(ss>>v)) continue;
        if(t=="wtime") g.wtime=v; else if(t=="btime") g.btime=v;
        else if(t=="winc") g.winc=v; else if(t=="binc") g.binc=v;
        else if(t=="movetime") g.movetime=v; else if(t=="depth") g.depth=v;
        else if(t=="nodes") g.nodes=v;
    }
    return g;
}

class UCIBudgeter : public Budgeter {
public:
    UCIBudgeter(int nodes, double secs) : _nodes(nodes), _secs(secs) {}
    void init() override { _start = Clock::now(); }
    bool should_exit(Position& pos) const override {
        double e = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-_start).count()/1e6;
        return pos.node_count >= _nodes || e >= _secs;
    }
private:
    int _nodes; double _secs; TimePoint _start;
};

// ---- Global state ----

static Position g_position = *Position::parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
static std::atomic<bool> g_should_stop{false};

// ---- Exported C API ----

extern "C" {

EMSCRIPTEN_KEEPALIVE void uci_init() {
    static CapturingBuf buf;
    std::cout.rdbuf(&buf);
    g_position = *Position::parse_fen(START_FEN);
}

// Process one UCI command synchronously. Search runs here (blocking).
EMSCRIPTEN_KEEPALIVE void uci_send(const char* cmd) {
    std::string line(cmd);

    if (line == "uci") {
        std::cout << "id name limerikk\n";
        std::cout << "id author Jeremy and Rikki\n";
        std::cout << "uciok\n";
    } else if (line == "isready") {
        std::cout << "readyok\n";
    } else if (line == "ucinewgame") {
        g_position = *Position::parse_fen(START_FEN);
    } else if (line.starts_with("position")) {
        parse_position(line, &g_position);
    } else if (line == "stop") {
        g_should_stop = true;
    } else if (line.starts_with("go")) {
        g_should_stop = false;
        GoParams g = parse_go(line);
        int time_ms = g.infinite ? INT32_MAX
                    : g.movetime ? *g.movetime
                    : [&]{
                          int t = g_position.to_move==WHITE ? g.wtime.value_or(0) : g.btime.value_or(0);
                          int i = g_position.to_move==WHITE ? g.winc.value_or(0)  : g.binc.value_or(0);
                          return t ? t/40+i/2 : INT32_MAX;
                      }();
        double time_s = double(time_ms)/1000.0*0.95;
        int nodes = g.nodes.value_or(INT_MAX);
        int depth = g.depth.value_or(40);

        UCIBudgeter budgeter(nodes, time_s);
        Move move = g_position.think(depth, g_should_stop, &budgeter, {}, true);
        std::cout << "bestmove " << to_uci_move(move) << "\n";
    }
}

// Returns all pending output as a null-terminated string (caller must not free).
// Empty string if nothing pending.
EMSCRIPTEN_KEEPALIVE const char* uci_flush() {
    static std::string buf;
    buf = std::move(g_output_buf);
    g_output_buf.clear();
    return buf.c_str();
}

} // extern "C"

int main() {
    emscripten_exit_with_live_runtime();
    return 0;
}
