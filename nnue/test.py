import sys
import subprocess
import os
import numpy as np
import torch

from model import NNUE

EVAL_BIN = os.path.join(os.path.dirname(__file__), "../build/eval/eval")
CHECKPOINT = os.path.join(os.path.dirname(__file__), "checkpoints/model_epoch30.pt")

# piece_id order matches to_bitboards: P=0, N=1, B=2, R=3, Q=4, K=5
PIECE_IDS = {'P': 0, 'N': 1, 'B': 2, 'R': 3, 'Q': 4, 'K': 5}

DEFAULT_FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
    "8/8/8/8/8/8/8/4K2k w - - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
]


def fen_to_features(fen):
    board_str = fen.split()[0]
    pieces = []
    rank = 7
    file = 0
    for ch in board_str:
        if ch == '/':
            rank -= 1
            file = 0
        elif ch.isdigit():
            file += int(ch)
        else:
            side = 0 if ch.isupper() else 1
            piece_id = PIECE_IDS[ch.upper()]
            sq = rank * 8 + file
            pieces.append((side, piece_id, sq))
            file += 1

    white_feats = [side * 6 * 64 + pid * 64 + sq for side, pid, sq in pieces]
    black_feats = [(1 - side) * 6 * 64 + pid * 64 + (sq ^ 56) for side, pid, sq in pieces]
    return white_feats, black_feats


def load_model(checkpoint_path):
    model = NNUE()
    state_dict = torch.load(checkpoint_path, map_location="cpu", weights_only=True)
    state_dict = {k.replace("_orig_mod.", ""): v for k, v in state_dict.items()}
    model.load_state_dict(state_dict)
    model.eval()
    return model


def torch_eval(model, fen):
    white_feats, black_feats = fen_to_features(fen)
    wf = torch.tensor(white_feats, dtype=torch.long)
    wi = torch.zeros(len(white_feats), dtype=torch.long)
    bf = torch.tensor(black_feats, dtype=torch.long)
    bi = torch.zeros(len(black_feats), dtype=torch.long)
    with torch.no_grad():
        return model(wf, wi, bf, bi).item()


def cpp_eval(fen):
    result = subprocess.run([EVAL_BIN, fen], capture_output=True, text=True)
    return float(result.stdout.strip())


def main():
    fens = sys.argv[1:] if len(sys.argv) > 1 else DEFAULT_FENS

    print(f"Loading model from {CHECKPOINT}")
    model = load_model(CHECKPOINT)

    print(f"{'FEN':<60} {'PyTorch':>10} {'C++ eval':>10} {'diff':>10}")
    print("-" * 94)

    for fen in fens:
        pt_score = torch_eval(model, fen)
        cpp_score = cpp_eval(fen)
        diff = pt_score - cpp_score
        short_fen = fen[:58] + ".." if len(fen) > 60 else fen
        print(f"{short_fen:<60} {pt_score:>10.6f} {cpp_score:>10.6f} {diff:>+10.6f}")


if __name__ == "__main__":
    main()
