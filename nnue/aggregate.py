import os
import glob
import numpy as np

with open("aggregate.bin", "wb") as out:
    for path in glob.glob(os.path.join("dataset", "*.bin")):
        data = open(path, "rb").read()
        out.write(data)
        print(f"Wrote {path}")