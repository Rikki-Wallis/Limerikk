EXE    = uci
MAKEFLAGS += --no-print-directory

.PHONY: all clean

all:
	cmake -B build -DCMAKE_BUILD_TYPE=Release > /dev/null
	cmake --build build --target uci -j$(nproc)
	cp build/uci/uci $(EXE)

clean:
	rm -rf build $(EXE)
