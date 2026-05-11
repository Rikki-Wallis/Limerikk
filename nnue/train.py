from model import *

import time
import multiprocessing

from torch.utils.data import DataLoader

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
#device = "cpu"

total = os.path.getsize("aggregate.bin") // RECORD_SIZE
#total = 200
split = int(total * 0.9)

train = NNUEDataset("aggregate.bin", 0, split)
val   = NNUEDataset("aggregate.bin", split, total)

import multiprocessing

torch.multiprocessing.set_sharing_strategy('file_system')

print(f"Training on {len(train)} dataset samples")

batch_size = 16384

from torch.utils.cpp_extension import load
ext = load(name="data_loader_ext", sources=["data_loader.cpp"], extra_cflags=['-O3'], verbose=True)

CUR_BLEND = multiprocessing.Value('f', 0.5)

def collate_fn(batch):
    return ext.collate_batch(batch, CUR_BLEND.value)

train_loader = DataLoader(train, batch_size=batch_size, shuffle=False, num_workers=12,persistent_workers=True,collate_fn=collate_fn, pin_memory=True)
val_loader = DataLoader(val, batch_size=batch_size, num_workers=12,persistent_workers=True,collate_fn=collate_fn)

model = NNUE()
model = torch.compile(model)
model = model.to(device)
#model.load_state_dict(torch.load("new.pt"))

num_epochs = 30

optimizer = torch.optim.AdamW(model.parameters(), lr=4e-3)

scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(
    optimizer,
    T_max=num_epochs,        # total epochs
    eta_min=3e-4     # final LR
)

criterion = nn.MSELoss() 

model.train()

start_blend = 0.0
end_blend = 0.5

start = time.perf_counter()

def lerp(a, b, t):
    return (1-t)*a + t*b

for epoch in range(num_epochs):
    train_loss = 0

    CUR_BLEND.value = lerp(start_blend, end_blend, epoch/(num_epochs-1))

    for i, (white_features, white_indices, black_features, black_indices, target) in enumerate(train_loader):

        target = target.to(device)
        white_features = white_features.to(device)
        white_indices  = white_indices.to(device)
        black_features = black_features.to(device)
        black_indices  = black_indices.to(device)

        optimizer.zero_grad()

        with torch.amp.autocast("cuda", dtype=torch.bfloat16):
            output = model(white_features, white_indices, black_features, black_indices)
            loss = criterion(output, target.float())

        loss.backward()
        optimizer.step()
        scheduler.step()

        train_loss += loss.item()

        if i % 20 == 0:
            elapsed = time.perf_counter() - start
            pps = batch_size * 20 / elapsed
            print(f"Batch {i+1}: {pps} pps (loss={loss.item()})")
            start = time.perf_counter()

    train_loss /= len(train_loader)

    model.eval()
    with torch.no_grad():
        val_loss = 0

        for wf, wi, bf, bi, tv in val_loader:
            tv = tv.to(device)
            wf = wf.to(device)
            wi = wi.to(device)
            bf = bf.to(device)
            bi = bi.to(device)

            val_loss += criterion(model(wf, wi, bf, bi), tv.float()).item()

        val_loss /= len(val_loader)

    print(f"Epoch {epoch+1} train: {train_loss:.6f} val: {val_loss:.6f} (wdl blend: {CUR_BLEND.value})")
    model.train()

    torch.save(model.state_dict(), f"model_epoch{epoch+1}_val{val_loss:.6f}.pt")