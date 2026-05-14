from model import *

import time
import numpy as np
from concurrent.futures import ThreadPoolExecutor

import dataloader

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

torch.multiprocessing.set_sharing_strategy('file_system')

batch_size = 16384

model = NNUE()
model = torch.compile(model)
model = model.to(device)
#model.load_state_dict(torch.load("new.pt"))

num_epochs = 30

optimizer = torch.optim.AdamW(model.parameters(), lr=4e-3)

scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(
    optimizer,
    T_max=num_epochs,
    eta_min=3e-4
)

criterion = nn.MSELoss()

model.train()

start_blend = 0.0
end_blend = 0.5

def lerp(a, b, t):
    return (1-t)*a + t*b

for epoch in range(num_epochs):
    loader = dataloader.Dataloader("aggregate.bin")
    CUR_BLEND = lerp(start_blend, end_blend, epoch/(num_epochs-1))

    start = time.perf_counter()

    with ThreadPoolExecutor(max_workers=1) as executor:
        future = executor.submit(loader.get_batch, batch_size, CUR_BLEND)

        for i in range(10000000000000):
            result = future.result()
            if result is None:
                break

            future = executor.submit(loader.get_batch, batch_size, CUR_BLEND)

            white_features, white_indices, black_features, black_indices, target = result

            target         = torch.from_numpy(np.array(target,         dtype=np.float32)).to(device, non_blocking=True)
            white_features = torch.from_numpy(np.array(white_features, dtype=np.int64  )).to(device, non_blocking=True)
            white_indices  = torch.from_numpy(np.array(white_indices,  dtype=np.int64  )).to(device, non_blocking=True)
            black_features = torch.from_numpy(np.array(black_features, dtype=np.int64  )).to(device, non_blocking=True)
            black_indices  = torch.from_numpy(np.array(black_indices,  dtype=np.int64  )).to(device, non_blocking=True)

            optimizer.zero_grad(set_to_none=True)

            with torch.amp.autocast("cuda", dtype=torch.bfloat16):
                output = model(white_features, white_indices, black_features, black_indices)
                loss = criterion(output, target)

            loss.backward()
            optimizer.step()

            if i % 20 == 0:
                elapsed = time.perf_counter() - start
                pps = batch_size * 20 / elapsed
                print(f"Batch {i+1}: {pps:.0f} pps (loss={loss.item():.6f})")
                start = time.perf_counter()

    scheduler.step()
    torch.save(model.state_dict(), f"checkpoints/model_epoch{epoch+1}.pt")
