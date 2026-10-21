// Snapshot interpolation: remote players are shown slightly in the past
// (InterpSettings::delayMs) so there are always two received states to blend
// between. If packets stop, the last state is extrapolated briefly, then held.
#pragma once

#include <cstdint>
#include <deque>

#include "coop/protocol.h"

namespace coop {

// Turns a wrapping 32-bit millisecond clock into a 64-bit one.
class TimeUnwrapper {
public:
    int64_t unwrap(uint32_t t);
    void reset() { has_ = false; }

private:
    bool has_ = false;
    int64_t last_ = 0;
};

// Estimates (local clock - remote clock). Uses the smallest offset seen in a
// sliding window: that packet had the least network delay, so the minimum
// filters out jitter.
class ClockOffsetEstimator {
public:
    explicit ClockOffsetEstimator(uint32_t windowMs = 2000) : windowMs_(windowMs) {}
    void observe(int64_t localMs, int64_t remoteMs);
    bool valid() const { return !samples_.empty(); }
    int64_t offset() const;
    void reset() { samples_.clear(); }

private:
    struct Sample {
        int64_t local;
        int64_t offset;
    };
    std::deque<Sample> samples_;
    uint32_t windowMs_;
};

struct InterpSettings {
    uint32_t delayMs = 100;
    uint32_t maxExtrapolationMs = 250;
    float teleportDistance = 25.f;  // jumps bigger than this snap instead of blending
};

struct SampleResult {
    PlayerState state;
    bool extrapolated = false;     // render time is past the newest state
    bool heldAtJump = false;       // waiting at a teleport/mount change instead of blending across it
    int32_t bufferAheadMs = 0;     // newest state time minus render time (negative = starved)
};

class SnapshotBuffer {
public:
    void push(const PlayerState& s, int64_t localNowMs);
    bool sample(int64_t localNowMs, const InterpSettings& cfg, SampleResult& out);
    bool empty() const { return snaps_.empty(); }
    size_t size() const { return snaps_.size(); }
    const PlayerState* latest() const { return snaps_.empty() ? nullptr : &snaps_.back().state; }
    void clear();

private:
    struct Snap {
        int64_t t;  // unwrapped sender time
        PlayerState state;
    };
    std::deque<Snap> snaps_;
    TimeUnwrapper unwrap_;
    ClockOffsetEstimator clock_;
};

PlayerState blendStates(const PlayerState& a, const PlayerState& b, float t);
PlayerState extrapolate(const PlayerState& s, float seconds);

// Turns a measured speed into a movement state (thresholds in m/s).
MoveState classifySpeed(float speedMps, bool mounted);

}  // namespace coop
