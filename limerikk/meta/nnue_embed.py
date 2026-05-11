import argparse
import struct

parser = argparse.ArgumentParser(description="Embed NNUE model into a C++ header")
parser.add_argument("model_bin_path", help="Path to the model binary file")
parser.add_argument("header_path", help="Path to the output header file")
args = parser.parse_args()

model_bin_path = args.model_bin_path
header_path = args.header_path

mb = open(model_bin_path, "rb")

def read_floats(n):
    global mb
    return list(struct.unpack(f"{n}f", mb.read(n*4)))

def read_float_matrix(rows, cols):
    return [read_floats(cols) for _ in range(rows)]

num_inputs = 2*6*64
l1_output = 64
l2_input = l1_output*2
l3_input = 32

# load the weights

w0 = read_float_matrix(l1_output, num_inputs)
b0 = read_floats(l1_output)

w1 = read_float_matrix(l3_input, l2_input)
b1 = read_floats(l3_input)

w2 = read_float_matrix(1, l3_input);
b2 = read_floats(1)

def transpose(m):
    rows = len(m)
    cols = len(m[0])

    result = [[0]*rows for _ in range(cols)]

    for i in range(rows):
        for j in range(cols):
            result[j][i] = m[i][j]
    
    return result

w0_t = transpose(w0)

# quantize the weights

def clamp(x, lo, hi):
    return min(max(x, lo), hi)

def quantize_matrix(m, q, bitwidth):
    lo = -(1 << (bitwidth-1))
    hi = (1 << (bitwidth-1))-1
    qm = [[clamp(int(round(q*x)), lo, hi) for x in row] for row in m]
    return qm

def quantize_vector(v, q, bitwidth):
    lo = -(1 << (bitwidth-1))
    hi = (1 << (bitwidth-1))-1
    qv = [clamp(int(round(q*x)), lo, hi) for x in v]
    return qv

w0_t = quantize_matrix(w0_t, 64, 8)
b0   = quantize_vector(b0, 64, 16)

w1   = quantize_matrix(w1, 64, 8)
b1   = quantize_vector(b1, 64*255, 32)

w2   = quantize_matrix(w2, 64, 16)
b2   = quantize_vector(b2, 64*255, 32)

# write the header

out = open(header_path, "w")

out.write("#pragma once\n\n")
out.write(f"constexpr size_t NNUE_INPUT_FEATURES = {num_inputs};\n")
out.write(f"constexpr size_t NNUE_ACCUMULATOR_PERSP_SIZE = {l1_output};\n\n")

def write_matrix(m, name, type):
    global out

    rows = len(m)
    cols = len(m[0])

    out.write(f"alignas(32) static const {type} nnue_{name}[{rows}][{cols}] = {{\n")
    for i in range(rows):
        out.write("    { ")
        for j in range(cols):
            if j > 0:
                out.write(", ")
            out.write(f"{m[i][j]}")
        out.write(" },\n")
    out.write("};\n\n");

def write_vector(v, name, type):
    global out

    n = len(v)

    out.write(f"alignas(32) static const {type} nnue_{name}[{n}] = {{\n")
    out.write("  ")

    for i in range(n):
        if i > 0:
            out.write(", ")
        out.write(f"{v[i]}")

    out.write("\n")
    out.write("};\n\n");

write_matrix(w0_t, "w0", "int8_t")
write_vector(b0,   "b0", "int16_t")
write_matrix(w1,   "w1", "int8_t")
write_vector(b1,   "b1", "int32_t")
write_matrix(w2,   "w2", "int16_t")
write_vector(b2,   "b2", "int32_t")