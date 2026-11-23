#include "audio.hpp"
#include "music.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <vector>

namespace {

constexpr uint32_t kChipClock = 7670453; // NTSC 68000 clock

std::vector<uint8_t> load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) SDL_Log("cannot load %s", path.c_str());
    return {std::istreambuf_iterator<char>(f), {}};
}

} // namespace

struct Audio::Impl {
    SDL_AudioStream* stream = nullptr;
    SDL_Mutex* mutex = nullptr;
    ymfm::ymfm_interface chip_intf;
    ymfm::ym2612 chip{chip_intf};
    std::unique_ptr<Music> music;
    std::string dir;
    int rate = 0;      // the chip's sample rate, clock / 144
    int tick_phase = 0; // the driver runs its sequencer at 60 Hz
    struct Voice {
        std::shared_ptr<std::vector<float>> data; // stereo float at rate
        size_t pos = 0;
        int channel = -1;
        float gain = 1.0f;
    };
    std::vector<Voice> voices;
    std::map<std::string, std::shared_ptr<std::vector<float>>> cache;
    std::vector<float> buf;

    static void SDLCALL callback(void* userdata, SDL_AudioStream* s, int additional, int) {
        Impl* self = static_cast<Impl*>(userdata);
        int frames = additional / int(sizeof(float) * 2);
        if (frames <= 0)
            return;
        self->buf.assign(size_t(frames) * 2, 0.0f);
        SDL_LockMutex(self->mutex);
        for (int i = 0; i < frames; i++) {
            if ((self->tick_phase += 60) >= self->rate) {
                self->tick_phase -= self->rate;
                self->music->tick();
            }
            ymfm::ym2612::output_data out;
            self->chip.generate(&out);
            // The music level of DGen (4 times MAME's).
            self->buf[i * 2] = out.data[0] / 16384.0f;
            self->buf[i * 2 + 1] = out.data[1] / 16384.0f;
        }
        for (auto& v : self->voices) {
            size_t n = std::min(self->buf.size(), v.data->size() - v.pos);
            for (size_t i = 0; i < n; i++)
                self->buf[i] += (*v.data)[v.pos + i] * v.gain;
            v.pos += n;
        }
        std::erase_if(self->voices, [](const Voice& v) { return v.pos >= v.data->size(); });
        SDL_UnlockMutex(self->mutex);
        SDL_PutAudioStreamData(s, self->buf.data(), int(self->buf.size() * sizeof(float)));
    }
};

Audio::Audio() : impl_(std::make_unique<Impl>()) {}

Audio::~Audio() {
    if (impl_->stream)
        SDL_DestroyAudioStream(impl_->stream);
    if (impl_->mutex)
        SDL_DestroyMutex(impl_->mutex);
}

bool Audio::init(const std::string& data_dir) {
    Impl& m = *impl_;
    m.dir = data_dir + "/";
    m.mutex = SDL_CreateMutex();
    m.chip.reset();
    m.rate = int(m.chip.sample_rate(kChipClock));
    m.music = std::make_unique<Music>(m.chip, load(m.dir + "music/patches.bin"));
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, m.rate};
    m.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Impl::callback, &m);
    if (!m.stream) {
        SDL_Log("audio: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(m.stream);
    return true;
}

void Audio::music_tempo(int bpm) {
    SDL_LockMutex(impl_->mutex);
    impl_->music->tempo(bpm);
    SDL_UnlockMutex(impl_->mutex);
}

void Audio::play_music(int seq) {
    Impl& m = *impl_;
    char name[32];
    std::snprintf(name, sizeof name, "music/seq%02d.bin", seq);
    std::vector<uint8_t> data = load(m.dir + name);
    if (data.empty())
        return;
    SDL_LockMutex(m.mutex);
    m.music->start(seq, data);
    SDL_UnlockMutex(m.mutex);
}

void Audio::stop_music(int seq) {
    SDL_LockMutex(impl_->mutex);
    impl_->music->stop(seq);
    SDL_UnlockMutex(impl_->mutex);
}

void Audio::stop_music() {
    SDL_LockMutex(impl_->mutex);
    impl_->music->stop_all();
    SDL_UnlockMutex(impl_->mutex);
}

void Audio::play_sfx(const std::string& path, int channel, float gain) {
    Impl& m = *impl_;
    auto it = m.cache.find(path);
    if (it == m.cache.end()) {
        SDL_AudioSpec spec;
        Uint8* wav = nullptr;
        Uint32 len = 0;
        std::shared_ptr<std::vector<float>> pcm;
        if (SDL_LoadWAV(path.c_str(), &spec, &wav, &len)) {
            SDL_AudioSpec dst{SDL_AUDIO_F32, 2, m.rate};
            Uint8* out = nullptr;
            int outlen = 0;
            if (SDL_ConvertAudioSamples(&spec, wav, int(len), &dst, &out, &outlen)) {
                pcm = std::make_shared<std::vector<float>>((float*)out, (float*)out + outlen / sizeof(float));
                SDL_free(out);
            }
            SDL_free(wav);
        } else {
            SDL_Log("cannot load %s: %s", path.c_str(), SDL_GetError());
        }
        it = m.cache.emplace(path, pcm).first;
    }
    if (!it->second)
        return;
    SDL_LockMutex(m.mutex);
    if (channel >= 0)
        std::erase_if(m.voices, [&](const Impl::Voice& v) { return v.channel == channel; });
    m.voices.push_back({it->second, 0, channel, gain});
    SDL_UnlockMutex(m.mutex);
}

void Audio::stop_sfx(int channel) {
    Impl& m = *impl_;
    SDL_LockMutex(m.mutex);
    std::erase_if(m.voices, [&](const Impl::Voice& v) { return v.channel == channel; });
    SDL_UnlockMutex(m.mutex);
}
