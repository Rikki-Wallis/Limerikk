# Limerikk

A modern chess engine written in C++20 with UCI protocol support, SPSA parameter tuning, self-trained NNUE and comprehensive testing. On Lichess as [BlunderfishEngine](https://lichess.org/@/BlunderfishEngine).

## Features

### Search
- Negamax with alpha-beta pruning and aspiration windows
- Transposition table (clustered buckets, Zobrist hashing)
- Null move pruning with adaptive reduction
- Singular extensions
- Reverse futility pruning
- Late move reductions (LMR) with history-based adjustments
- Late move pruning
- Futility pruning
- Quiescence search with delta pruning
- Killer moves, history heuristic, and continuation history
- MVV-LVA with static exchange evaluation (SEE) for capture ordering
- Threefold repetition detection

### Evaluation
- Tapered evaluation (middlegame/endgame interpolation)
- Piece-square tables
- King safety (castling rights, pawn shelter, open files)
- Pawn structure (isolated, doubled, connected, passed pawns)
- Bishop pair bonus
- Working on a Efficiently Updatable Neural Network (NNUE)

### Board Representation
- Bitboards with magic bitboard move generation
- Pre-computed lookup tables for king, knight, and sliding piece moves

### Other
- UCI protocol support
- SPSA parameter tuning via self-play
- Perft testing
- Benchmarking suite
- Pytorch training
- NNUE data-set generation via self-play

## Building

Requires CMake 3.16+ and a C++20 compiler.

```bash
mkdir build && cd build
cmake .. -G Ninja
ninja
```

This builds the following targets:

| Target | Description |
|---|---|
| `uci` | UCI protocol interface |
| `benchmark` | Performance benchmarking tool |
| `spsa` | SPSA parameter tuning via self-play |
| `datagen` | Position-label generator for training NNUE |
| `precompute_tables` | Magic bitboard table generator (runs at build time) |

Tests are built automatically and can be run with:

```bash
ctest
```

## Testing

The test suite uses [Catch2](https://github.com/catchorg/Catch2) and covers:

- **Perft** — move generation correctness against known node counts
- **FEN** — encoding/decoding roundtrip verification
- **Zobrist** — hash function validation
- **Eval** — incremental evaluation function output verification
- **Best move** — search result correctness
- **Misc** — chess rules and utility functions

## SPSA Tuning

The SPSA tuner optimizes search parameters through self-play tournaments. It tunes LMR rates, pruning margins, history bonus factors, aspiration window sizes, null move pruning depth, and more.

```bash
./build/spsa/spsa
```

## License

MIT — see [LICENSE](LICENSE) for details.
