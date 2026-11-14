// HUD: message box, enemy counter and health bar (0x1E00E-0x1E330, 0xD98E).
#include "display.hpp"
#include "game.hpp"

#include "../host.hpp"

#include <cstring>
#include <sstream>

namespace game {

namespace {
auto& msg_count = st.msg_count; // queued messages
auto& msg_timer = st.msg_timer; // frames left for the shown message

struct Message {
    char text[36]; // 4 lines of 9 characters
    std::vector<Speech> speech;
};

// data/text/messages.txt, then Msg::Saved.
const std::vector<Message>& messages() {
    static std::vector<Message> v;
    if (v.empty()) {
        for (const std::string& line : assets::lines("text/messages.txt")) {
            Message& m = v.emplace_back();
            for (int i = 0; i < 36; i++) m.text[i] = line[i / 9 * 10 + i % 9];
            std::istringstream ss(line.substr(40));
            Speech s;
            while (ss >> std::hex >> s.time >> s.sound) m.speech.push_back(s);
        }
        Message& m = v.emplace_back();
        std::memcpy(m.text, "GAME     SAVED                      ", 36);
    }
    return v;
}
}

void queue_saved_message() { QueueMessage(Msg::Saved); }

// 0x1E00E: two-digit enemy counter.
void DrawEnemyCounter(uint16_t n) {
    while (fb_dma_pending) host::frame();
    uint16_t tens = 0;
    while (n >= 10) {
        tens++;
        n = uint16_t(n - 10);
    }
    display::write_cell(0xC482, uint16_t(tens + 0xE58E));
    display::write_cell(0xC484, uint16_t(n + 0xE58E));
}

// 0x1E0C2: queues a message (4 lines of 9 characters, then a speech list).
void QueueMessage(Msg msg) {
    uint16_t n = msg_count;
    if (n == 0x10) return;
    msg_count += 1;
    st.msg_queue[n] = msg;
}

// 0x1E0DE
void ClearMessages() {
    msg_count = 0;
    msg_timer = 0;
    st.speech[0].time = 0xFFFF;
    st.speech_timer = 0;
    st.speech_on = 0;
    st.dac_hold = 0;
}

// 0x1E2CC: plays the speech samples of the shown message one after another
// (FF08CC is counted down by VBlank).
void PlaySpeech() {
    if (!st.speech_on) {
        st.speech[0].time = 0xFFFF;
        return;
    }
    if (st.speech[0].time == 0xFFFF || int16_t(uint16_t(st.speech_timer)) >= 0) return;
    for (int i = 0; i < 7; i++) st.speech[i] = st.speech[i + 1];
    if (st.speech[0].time == 0xFFFF) {
        st.speech_on = 0;
        return;
    }
    st.speech_timer = st.speech[0].time;
    PlaySoundLocal(st.speech[0].sound);
}

// 0x1E0FA: level-cleared countdown and the message box.
void UpdateMessages() {
    auto& cleared = st.cleared_timer;
    if (cleared == 0x69) {
        cleared -= 1;
        QueueMessage(Msg::ProceedNext);
    } else if (cleared) {
        cleared -= 1;
        if (!cleared) {
            cleared = 0xE1;
            QueueMessage(Msg::FloorSecured);
        }
    }
    PlaySpeech();
    if (msg_timer) {
        int16_t left = int16_t(msg_timer - msg_count - 1);
        if (left > 0) {
            msg_timer = uint16_t(left);
            return;
        }
        msg_timer = 0;
        for (int r = 0; r < 4; r++) display::fill_cells(uint16_t(0xCA04 + r * 0x80), 9, 0xE573);
        return;
    }
    if (!msg_count) return;
    Msg id = st.msg_queue[0];
    for (int i = 0; i < 15; i++) st.msg_queue[i] = st.msg_queue[i + 1];
    msg_count -= 1;
    msg_timer = 0x14;
    while (fb_dma_pending) host::frame();
    const Message& m = messages()[size_t(id)];
    for (int r = 0; r < 4; r++) {
        uint16_t nt = uint16_t(0xCA04 + r * 0x80);
        for (int i = 0; i < 9; i++) display::write_cell(uint16_t(nt + i * 2), kMsgFont[uint8_t(m.text[r * 9 + i] - 0x20)]);
    }
    if (id == Msg::Saved || !(uint8_t(st.sound_options) & 2) || st.speech_on) return;
    if (m.speech.empty()) {
        st.speech_on = 0;
        return;
    }
    Speech* dst = st.speech;
    for (const Speech& s : m.speech) *dst++ = s;
    dst->time = 0xFFFF;
    st.speech_on = 1;
    st.speech_timer = st.speech[0].time;
    PlaySoundLocal(st.speech[0].sound);
}

// 0xD98E: health bar tile, coloured by the amount left.
void DrawHealthBar() {
    while (fb_dma_pending) host::frame();
    uint16_t tile = display::planeB[0x265];
    uint16_t dest = uint16_t((tile & 0x7FF) * 32);
    int16_t h = int16_t(uint16_t(st.health));
    if (h < 0) {
        h = 0;
        st.health = 0;
    } else if (h > 0x64) {
        h = 0x64;
        st.health = 0x64;
    }
    int32_t d0 = h * 0x64 - 1;
    if (d0 < 0) d0 = 0;
    uint16_t pct = uint16_t(uint32_t(d0) / 0x64);
    uint32_t d1 = pct > 0x50 ? 0x77777777 : pct > 0x3C ? 0x76767676 : pct > 0x28 ? 0x66666666
                : pct > 0x14 ? 0x65656565 : 0x55555555;
    uint32_t d3 = d1 >> 4 | d1 << 28;
    const uint8_t* digits = assets::tiles("screens/tiles/hud_health_digits.png").data();
    const uint8_t* a0 = digits;
    uint16_t ones = pct;
    while (ones >= 10) {
        ones = uint16_t(ones - 10);
        a0 += 0x20;
    }
    for (int i = 0; i < 8; i++, a0 += 4) display::tile_long(uint16_t(dest + i * 4), be32(a0) | (i & 1 ? d3 : d1));
    a0 = digits + (ones << 5);
    for (int i = 0; i < 8; i++, a0 += 4)
        display::tile_long(uint16_t(dest + 32 + i * 4), be32(a0) | (i & 1 ? d3 : d1));
}

} // namespace game
