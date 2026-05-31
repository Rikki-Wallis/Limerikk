from model import *

import time
import numpy as np
from concurrent.futures import ThreadPoolExecutor

import dataloader

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

torch.multiprocessing.set_sharing_strategy('file_system')

batch_size = 16384
super_batch_size = 100_000_000
super_batch_batch_count = (super_batch_size + batch_size-1) // batch_size
num_super_batches = 30

model = NNUE()
model = torch.compile(model)
model = model.to(device)
#model.load_state_dict(torch.load("new.pt"))

optimizer = torch.optim.AdamW(model.parameters(), lr=4e-3)

scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(
    optimizer,
    T_max=num_super_batches,
    eta_min=3e-4
)

criterion = nn.MSELoss()

model.train()

start_blend = 0.0
end_blend = 0.4

loader = dataloader.Dataloader("aggregate.bin")

def lerp(a, b, t):
    return (1-t)*a + t*b

for sb in range(num_super_batches):
    start = time.perf_counter()

    CUR_BLEND = lerp(start_blend, end_blend, sb/(num_super_batches-1))

    for b in range(super_batch_batch_count):
        result = loader.get_batch(batch_size, CUR_BLEND)

        if result is None:
            loader = dataloader.Dataloader("aggregate.bin")
            result = loader.get_batch(batch_size, CUR_BLEND)

        white_features, white_indices, black_features, black_indices, target = result

        target         = torch.from_numpy(target).to(device, non_blocking=True)
        white_features = torch.from_numpy(white_features).to(device, non_blocking=True)
        white_indices  = torch.from_numpy(white_indices ).to(device, non_blocking=True)
        black_features = torch.from_numpy(black_features).to(device, non_blocking=True)
        black_indices  = torch.from_numpy(black_indices ).to(device, non_blocking=True)

        optimizer.zero_grad(set_to_none=True)

        with torch.amp.autocast("cuda", dtype=torch.bfloat16):
            output = model(white_features, white_indices, black_features, black_indices)
            loss = criterion(output, target)

        loss.backward()
        optimizer.step()

        if b % 20 == 0:
            elapsed = time.perf_counter() - start
            pps = batch_size * 20 / elapsed
            print(f"Superbatch {sb+1}/{num_super_batches} batch {b+1}/{super_batch_batch_count}: {pps:.0f} pps (loss={loss.item():.6f})")
            start = time.perf_counter()

    scheduler.step()
    torch.save(model.state_dict(), f"checkpoints/model_sb{sb+1}.pt")
