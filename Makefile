BUILD := build
WIN_BUILD := $(BUILD)/win
WIN_DEPS := $(BUILD)/win-deps
SDL3_VER := 3.4.20
FLUIDSYNTH_VER := 2.6.2
SDL3_WIN := $(WIN_DEPS)/SDL3-$(SDL3_VER)/x86_64-w64-mingw32
FLUIDSYNTH_WIN := $(WIN_DEPS)/fluidsynth/fluidsynth-v$(FLUIDSYNTH_VER)-win10-x64-cpp11

.PHONY: all zt win clean

all: zt

$(BUILD)/CMakeCache.txt:
	cmake -S . -B $(BUILD)

zt: | $(BUILD)/CMakeCache.txt
	cmake --build $(BUILD) -j --target zt

$(SDL3_WIN):
	mkdir -p $(WIN_DEPS)
	curl -fL https://github.com/libsdl-org/SDL/releases/download/release-$(SDL3_VER)/SDL3-devel-$(SDL3_VER)-mingw.tar.gz | tar xz -C $(WIN_DEPS)

$(FLUIDSYNTH_WIN):
	mkdir -p $(WIN_DEPS)
	curl -fLo $(WIN_DEPS)/fluidsynth.zip https://github.com/FluidSynth/fluidsynth/releases/download/v$(FLUIDSYNTH_VER)/fluidsynth-v$(FLUIDSYNTH_VER)-win10-x64-cpp11.zip
	unzip -qo $(WIN_DEPS)/fluidsynth.zip -d $(WIN_DEPS)/fluidsynth

$(WIN_BUILD)/CMakeCache.txt: | $(SDL3_WIN) $(FLUIDSYNTH_WIN)
	cmake -S . -B $(WIN_BUILD) -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
		"-DCMAKE_PREFIX_PATH=$(abspath $(SDL3_WIN));$(abspath $(FLUIDSYNTH_WIN))"

# Cross-compiled Windows build: zt.exe and its DLLs next to data/.
win: | $(WIN_BUILD)/CMakeCache.txt
	cmake --build $(WIN_BUILD) -j --target zt
	cp $(SDL3_WIN)/bin/SDL3.dll $(FLUIDSYNTH_WIN)/bin/libfluidsynth-3.dll \
		$(FLUIDSYNTH_WIN)/bin/sndfile.dll .

clean:
	rm -rf $(BUILD) zt zt.exe SDL3.dll libfluidsynth-3.dll sndfile.dll
