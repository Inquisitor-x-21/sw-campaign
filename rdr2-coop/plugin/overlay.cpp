#include "overlay.h"

#include "game.h"

namespace {

constexpr float kLeft = 0.012f;
constexpr float kTop = 0.015f;
constexpr float kLine = 0.024f;
constexpr float kScale = 0.30f;
constexpr size_t kMaxMessages = 5;

}  // namespace

void Overlay::message(const std::string& text, int64_t nowMs, Tone tone, uint32_t durationMs) {
    messages_.push_back({text, nowMs + durationMs, tone});
    while (messages_.size() > kMaxMessages) messages_.pop_front();
}

void Overlay::draw(const std::vector<std::string>& statusLines, bool panelVisible, int64_t nowMs) {
    while (!messages_.empty() && messages_.front().until < nowMs) messages_.pop_front();

    float y = kTop;
    if (panelVisible) {
        for (size_t i = 0; i < statusLines.size(); ++i) {
            if (i == 0) {
                game::drawText(kLeft, y, statusLines[i].c_str(), kScale, 235, 200, 120);  // title in gold
            } else {
                game::drawText(kLeft, y, statusLines[i].c_str(), kScale, 240, 240, 240);
            }
            y += kLine;
        }
        y += kLine * 0.5f;
    }
    for (const Message& m : messages_) {
        const int64_t left = m.until - nowMs;
        const int alpha = left < 1000 ? static_cast<int>(255 * left / 1000) : 255;  // fade out
        switch (m.tone) {
            case Tone::Good: game::drawText(kLeft, y, m.text.c_str(), kScale, 150, 230, 150, alpha); break;
            case Tone::Warn: game::drawText(kLeft, y, m.text.c_str(), kScale, 255, 140, 110, alpha); break;
            default: game::drawText(kLeft, y, m.text.c_str(), kScale, 220, 220, 220, alpha); break;
        }
        y += kLine;
    }
}
