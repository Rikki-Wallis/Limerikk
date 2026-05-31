import struct
import mmap
import os
import numpy as np

import torch
import torch.nn as nn
import torch.nn.functional as F

torch.set_float32_matmul_precision('high')

class NNUE(nn.Module):
    def __init__(self):
        super().__init__()

        self.l1 = nn.Linear(2*6*64, 64)
        self.l2 = nn.Linear(128, 1)

    @torch.compiler.disable
    def feed_l1(self, features, indices):
        f = self.l1.weight[:, features].T
        a1 = torch.zeros(indices.max()+1, 64, device=features.device)
        a1.index_add_(0, indices, f)
        return a1 + self.l1.bias

    def forward(self, white_features, white_indices, black_features, black_indices):
        a1w = self.feed_l1(white_features, white_indices)
        a1b = self.feed_l1(black_features, black_indices)

        a1 = torch.concat([a1w, a1b], dim=1)
        a1 = F.hardtanh(a1, 0, 1)

        a2 = F.sigmoid(self.l2(a1))

        return a2.squeeze(-1)
