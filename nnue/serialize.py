from model import *

import argparse
import numpy as np

parser = argparse.ArgumentParser(description="Serialize a pytorch model into a binary")
parser.add_argument("pt", help="Path to the pytorch model file")
args = parser.parse_args()

model = NNUE()
state_dict = torch.load(args.pt, map_location="cpu")
state_dict = {k.replace("_orig_mod.", ""): v for k, v in state_dict.items()}
model.load_state_dict(state_dict)
model.eval()

layers = [model.l1, model.l2]

with open("model.bin", "wb") as f:
    for layer in layers:
        w = layer.weight.detach().numpy().astype(np.float32)
        b = layer.bias.detach().numpy().astype(np.float32)
        w.tofile(f)
        b.tofile(f)