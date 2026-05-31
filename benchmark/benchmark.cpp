#include <cmath>

#include "limerikk.h"

int main() {
    std::unique_ptr<SearchContext> ctx = std::make_unique<SearchContext>(&null_budgeter);

    for (int depth = 1; depth <= 10; ++depth) {
        Position pos = *Position::parse_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");

        SearchStatistics stats;
        pos.best_move(*ctx, depth, false, nullptr, &stats);

        print("Best-move depth {}:\n", depth);
        print("----------------------\n");
        print("  time: {:.3f}ms\n", stats.time*1000.0f);
        print("  nodes: {}\n", stats.nodes);
        print("  qnodes: {} ({:.2f}%)\n", stats.qnodes, float(stats.qnodes)/float(stats.nodes)*100.0f);
        print("  pv-nodes: {} ({:.2f}%)\n", stats.pv_nodes, float(stats.pv_nodes)/float(stats.nodes)*100.0f);
        print("  rfps: {} ({:.2f}%)\n", stats.rfps, float(stats.rfps)/float(stats.nodes)*100.0f);
        print("  nmps: {} ({:.2f}%)\n", stats.nmps, float(stats.nmps)/float(stats.nodes)*100.0f);
        print("  mean-cutoff: {:.2f}\n", stats.mean_cutoff_index);
        print("  nps: {:.2f}M\n", float(stats.nodes)/stats.time/1000000);
        print("  ebf: {:.2f}\n", std::exp(std::log(float(stats.nodes))/float(depth)));
        print("  sel-depth: {}\n", stats.sel_depth);
        print("  tt-hit: {:.2f}%\n", stats.tt_hit_rate * 100.0f);
        print("\n");
    }
}