BUILD := build
WIN_BUILD := $(BUILD)/win
WIN_DEPS := $(BUILD)/win-deps
SDL3_VER := 3.4.20
SDL3_WIN := $(WIN_DEPS)/SDL3-$(SDL3_VER)/x86_64-w64-mingw32

.PHONY: all zt win clean

all: zt

$(BUILD)/CMakeCache.txt:
	cmake -S . -B $(BUILD)

zt: | $(BUILD)/CMakeCache.txt
	cmake --build $(BUILD) -j --target zt

$(SDL3_WIN):
	mkdir -p $(WIN_DEPS)
	curl -fL https://github.com/libsdl-org/SDL/releases/download/release-$(SDL3_VER)/SDL3-devel-$(SDL3_VER)-mingw.tar.gz | tar xz -C $(WIN_DEPS)

$(WIN_BUILD)/CMakeCache.txt: | $(SDL3_WIN)
	cmake -S . -B $(WIN_BUILD) -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
		-DCMAKE_PREFIX_PATH=$(abspath $(SDL3_WIN))

# Cross-compiled Windows build: zt.exe and its DLLs next to data/.
win: | $(WIN_BUILD)/CMakeCache.txt
	cmake --build $(WIN_BUILD) -j --target zt
	cp $(SDL3_WIN)/bin/SDL3.dll .

clean:
	rm -rf $(BUILD) zt zt.exe SDL3.dll
