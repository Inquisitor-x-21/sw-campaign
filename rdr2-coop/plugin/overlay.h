// Corner text: session status plus short-lived messages (joins, leaves,
// errors). Has to be drawn every frame.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class Overlay {
public:
    enum class Tone { Info, Good, Warn };

    void message(const std::string& text, int64_t nowMs, Tone tone = Tone::Info, uint32_t durationMs = 6000);
    // statusLines are only drawn when the panel is visible; messages always are.
    void draw(const std::vector<std::string>& statusLines, bool panelVisible, int64_t nowMs);

private:
    struct Message {
        std::string text;
        int64_t until;
        Tone tone;
    };
    std::deque<Message> messages_;
};
