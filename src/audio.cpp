#include "audio.hpp"

#include <SDL3/SDL.h>
#include <fluidsynth.h>

#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace {

constexpr int kRate = 44100;

// Finds the ticks of "loopStart" / "loopEnd" marker meta events in a MIDI file.
void find_loop_markers(const std::string& path, int& start, int& end) {
    start = end = -1;
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    size_t p = 14;
    auto u32 = [&](size_t i) { return uint32_t(d[i] << 24 | d[i + 1] << 16 | d[i + 2] << 8 | d[i + 3]); };
    while (p + 8 <= d.size()) {
        uint32_t len = u32(p + 4);
        size_t q = p + 8, e = std::min(d.size(), q + len);
        bool is_track = d[p] == 'M' && d[p + 1] == 'T' && d[p + 2] == 'r' && d[p + 3] == 'k';
        int tick = 0;
        uint8_t status = 0;
        auto var = [&]() {
            uint32_t v = 0;
            while (q < e) {
                uint8_t b = d[q++];
                v = (v << 7) | (b & 127);
                if (!(b & 128))
                    break;
            }
            return v;
        };
        while (is_track && q < e) {
            tick += var();
            if (q >= e)
                break;
            uint8_t b = d[q];
            if (b == 0xFF) {
                uint8_t type = d[q + 1];
                q += 2;
                uint32_t n = var();
                std::string text(d.begin() + q, d.begin() + std::min(e, q + n));
                if (type == 6 && text == "loopStart") start = tick;
                if (type == 6 && text == "loopEnd") end = tick;
                q += n;
            } else if (b == 0xF0 || b == 0xF7) {
                q++;
                q += var();
            } else {
                if (b & 0x80) {
                    status = b;
                    q++;
                }
                int hi = status >> 4;
                q += (hi == 0xC || hi == 0xD) ? 1 : 2;
            }
        }
        p = e;
    }
}

} // namespace

struct Audio::Impl {
    SDL_AudioStream* stream = nullptr;
    fluid_settings_t* settings = nullptr;
    fluid_synth_t* synth = nullptr;
    fluid_player_t* player = nullptr;
    SDL_Mutex* mutex = nullptr;
    struct Voice {
        std::shared_ptr<std::vector<float>> data; // stereo float at kRate
        size_t pos = 0;
        int channel = -1;
    };
    bool music_paused = false;
    std::vector<Voice> voices;
    std::map<std::string, std::shared_ptr<std::vector<float>>> cache;
    int loop_start = -1, loop_end = -1;
    float music_vol = 0.8f, sfx_vol = 1.0f;
    std::vector<float> buf, mbuf;

    static int on_tick(void* data, int tick) {
        Impl* self = static_cast<Impl*>(data);
        if (self->loop_end > 0 && tick >= self->loop_end)
            fluid_player_seek(self->player, std::max(self->loop_start, 0));
        return FLUID_OK;
    }

    static void SDLCALL callback(void* userdata, SDL_AudioStream* s, int additional, int) {
        Impl* self = static_cast<Impl*>(userdata);
        int frames = additional / int(sizeof(float) * 2);
        if (frames <= 0)
            return;
        self->buf.assign(size_t(frames) * 2, 0.0f);
        self->mbuf.assign(size_t(frames) * 2, 0.0f);
        SDL_LockMutex(self->mutex);
        if (self->player && !self->music_paused && fluid_player_get_status(self->player) == FLUID_PLAYER_PLAYING)
            fluid_synth_write_float(self->synth, frames, self->mbuf.data(), 0, 2, self->mbuf.data(), 1, 2);
        for (size_t i = 0; i < self->buf.size(); i++)
            self->buf[i] = self->mbuf[i] * self->music_vol;
        for (auto& v : self->voices) {
            size_t n = std::min(self->buf.size(), v.data->size() - v.pos);
            for (size_t i = 0; i < n; i++)
                self->buf[i] += (*v.data)[v.pos + i] * self->sfx_vol;
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
    if (impl_->player)
        delete_fluid_player(impl_->player);
    if (impl_->synth)
        delete_fluid_synth(impl_->synth);
    if (impl_->settings)
        delete_fluid_settings(impl_->settings);
    if (impl_->mutex)
        SDL_DestroyMutex(impl_->mutex);
}

bool Audio::init(const std::string& soundfont) {
    Impl& m = *impl_;
    m.mutex = SDL_CreateMutex();
    // FluidSynth only renders samples here (SDL plays them). Without this,
    // new_fluid_settings() probes every ALSA/JACK/OSS device and logs errors.
    static const char* drivers[] = {"file", nullptr};
    fluid_audio_driver_register(drivers);
    m.settings = new_fluid_settings();
    fluid_settings_setnum(m.settings, "synth.sample-rate", kRate);
    fluid_settings_setstr(m.settings, "player.timing-source", "sample");
    m.synth = new_fluid_synth(m.settings);
    if (fluid_synth_sfload(m.synth, soundfont.c_str(), 1) == FLUID_FAILED)
        SDL_Log("cannot load soundfont %s; music disabled", soundfont.c_str());
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kRate};
    m.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Impl::callback, &m);
    if (!m.stream) {
        SDL_Log("audio: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(m.stream);
    return true;
}

void Audio::play_music(const std::string& path) {
    Impl& m = *impl_;
    if (!m.synth)
        return;
    int ls, le;
    find_loop_markers(path, ls, le);
    SDL_LockMutex(m.mutex);
    if (m.player) {
        fluid_player_stop(m.player);
        delete_fluid_player(m.player);
    }
    fluid_synth_system_reset(m.synth);
    m.player = new_fluid_player(m.synth);
    m.loop_start = ls;
    m.loop_end = le;
    fluid_player_set_tick_callback(m.player, Impl::on_tick, &m);
    if (le < 0)
        fluid_player_set_loop(m.player, -1);
    if (fluid_player_add(m.player, path.c_str()) == FLUID_OK)
        fluid_player_play(m.player);
    else
        SDL_Log("cannot load music %s", path.c_str());
    SDL_UnlockMutex(m.mutex);
}

void Audio::stop_music() {
    Impl& m = *impl_;
    SDL_LockMutex(m.mutex);
    if (m.player) {
        fluid_player_stop(m.player);
        delete_fluid_player(m.player);
        m.player = nullptr;
    }
    if (m.synth)
        fluid_synth_all_sounds_off(m.synth, -1);
    SDL_UnlockMutex(m.mutex);
}

void Audio::play_sfx(const std::string& path, int channel) {
    Impl& m = *impl_;
    auto it = m.cache.find(path);
    if (it == m.cache.end()) {
        SDL_AudioSpec spec;
        Uint8* wav = nullptr;
        Uint32 len = 0;
        std::shared_ptr<std::vector<float>> pcm;
        if (SDL_LoadWAV(path.c_str(), &spec, &wav, &len)) {
            SDL_AudioSpec dst{SDL_AUDIO_F32, 2, kRate};
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
    m.voices.push_back({it->second, 0, channel});
    SDL_UnlockMutex(m.mutex);
}

void Audio::stop_sfx(int channel) {
    Impl& m = *impl_;
    SDL_LockMutex(m.mutex);
    std::erase_if(m.voices, [&](const Impl::Voice& v) { return v.channel == channel; });
    SDL_UnlockMutex(m.mutex);
}

void Audio::pause_music(bool paused) {
    SDL_LockMutex(impl_->mutex);
    impl_->music_paused = paused;
    SDL_UnlockMutex(impl_->mutex);
}

void Audio::set_volumes(float music, float sfx) {
    SDL_LockMutex(impl_->mutex);
    impl_->music_vol = music;
    impl_->sfx_vol = sfx;
    SDL_UnlockMutex(impl_->mutex);
}

bool Sounds::load(Audio* audio, const std::string& data_dir) {
    audio_ = audio;
    dir_ = data_dir + "/";
    std::ifstream f(data_dir + "/sound/sounds.txt");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string id, type, file;
        if (!(ss >> id >> type >> file) || id[0] == '#')
            continue;
        int n = std::stoi(id, nullptr, 16) & 255;
        file_[n] = file;
        music_[n] = type == "music";
    }
    return bool(f.eof());
}

void Sounds::play(int id) {
    id &= 255;
    if (!audio_ || file_[id].empty())
        return;
    if (music_[id]) {
        last_music_ = id;
        if (music_on_)
            audio_->play_music(dir_ + file_[id]);
    } else if (sfx_on_) {
        audio_->play_sfx(dir_ + file_[id]);
    }
}

void Sounds::set_enabled(bool music, bool sfx) {
    sfx_on_ = sfx;
    if (music == music_on_)
        return;
    music_on_ = music;
    if (!audio_)
        return;
    if (!music)
        audio_->stop_music();
    else if (last_music_ >= 0)
        audio_->play_music(dir_ + file_[last_music_]);
}
