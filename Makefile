BUILD := build

.PHONY: all zt test clean

all: zt

$(BUILD)/CMakeCache.txt:
	cmake -S . -B $(BUILD)

zt: | $(BUILD)/CMakeCache.txt
	cmake --build $(BUILD) -j --target zt

test: | $(BUILD)/CMakeCache.txt
	cmake --build $(BUILD) -j --target zt_test m68k ztref
	.venv/bin/python tools/difftest.py

clean:
	rm -rf $(BUILD) zt
