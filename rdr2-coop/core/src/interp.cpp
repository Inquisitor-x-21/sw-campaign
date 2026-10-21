#include "coop/interp.h"

#include <algorithm>
#include <iterator>

namespace coop {

int64_t TimeUnwrapper::unwrap(uint32_t t) {
    if (!has_) {
        has_ = true;
        last_ = static_cast<int64_t>(t);
        return last_;
    }
    const int32_t delta = static_cast<int32_t>(t - static_cast<uint32_t>(last_));
    const int64_t value = last_ + delta;
    if (delta > 0) last_ = value;
    return value;
}

void ClockOffsetEstimator::observe(int64_t localMs, int64_t remoteMs) {
    samples_.push_back({localMs, localMs - remoteMs});
    while (!samples_.empty() && samples_.front().local < localMs - static_cast<int64_t>(windowMs_)) {
        samples_.pop_front();
    }
    while (samples_.size() > 512) samples_.pop_front();
}

int64_t ClockOffsetEstimator::offset() const {
    int64_t best = samples_.front().offset;
    for (const Sample& s : samples_) best = std::min(best, s.offset);
    return best;
}

void SnapshotBuffer::clear() {
    snaps_.clear();
    unwrap_.reset();
    clock_.reset();
}

void SnapshotBuffer::push(const PlayerState& s, int64_t localNowMs) {
    const int64_t t = unwrap_.unwrap(s.timeMs);
    clock_.observe(localNowMs, t);

    if (snaps_.empty() || t > snaps_.back().t) {
        snaps_.push_back({t, s});
    } else {
        // Late or duplicate packet: keep the buffer sorted, ignore exact duplicates.
        auto it = std::upper_bound(snaps_.begin(), snaps_.end(), t,
                                   [](int64_t value, const Snap& snap) { return value < snap.t; });
        if (it != snaps_.begin() && std::prev(it)->t == t) return;
        snaps_.insert(it, {t, s});
    }

    const int64_t newest = snaps_.back().t;
    while (snaps_.size() > 2 && snaps_.front().t < newest - 3000) snaps_.pop_front();
    while (snaps_.size() > 64) snaps_.pop_front();
}

bool SnapshotBuffer::sample(int64_t localNowMs, const InterpSettings& cfg, SampleResult& out) {
    if (snaps_.empty() || !clock_.valid()) return false;

    out = SampleResult{};
    const int64_t renderT = localNowMs - clock_.offset() - static_cast<int64_t>(cfg.delayMs);
    const Snap& newest = snaps_.back();
    out.bufferAheadMs = static_cast<int32_t>(std::clamp<int64_t>(newest.t - renderT, -100000, 100000));

    if (renderT >= newest.t) {
        const int64_t ahead = renderT - newest.t;
        const int64_t used = std::min<int64_t>(ahead, cfg.maxExtrapolationMs);
        out.state = extrapolate(newest.state, static_cast<float>(used) / 1000.f);
        out.extrapolated = ahead > 0;
        return true;
    }
    if (renderT <= snaps_.front().t) {
        out.state = snaps_.front().state;
        return true;
    }

    size_t i = 0;
    while (i + 1 < snaps_.size() && snaps_[i + 1].t <= renderT) ++i;
    const Snap& a = snaps_[i];
    const Snap& b = snaps_[i + 1];

    const float span = static_cast<float>(b.t - a.t);
    const float frac = span > 0.f ? static_cast<float>(renderT - a.t) / span : 1.f;
    const float jumpDist = distance(a.state.pos, b.state.pos);
    const bool mountChanged = a.state.mounted() != b.state.mounted();
    const bool mountJump = a.state.mounted() && b.state.mounted() &&
                           distance(a.state.mountPos, b.state.mountPos) > cfg.teleportDistance;
    const bool flaggedJump = (b.state.flags & StateFlag::Teleported) != 0 && jumpDist > 3.f;
    if (jumpDist > cfg.teleportDistance || mountChanged || mountJump || flaggedJump) {
        out.state = a.state;  // hold, then switch to b once render time reaches it
        out.heldAtJump = true;
    } else {
        out.state = blendStates(a.state, b.state, frac);
    }

    // Everything before 'a' is in the past for good.
    while (i > 0) {
        snaps_.pop_front();
        --i;
    }
    return true;
}

PlayerState blendStates(const PlayerState& a, const PlayerState& b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    PlayerState r = t < 0.5f ? a : b;  // discrete fields come from the nearer state
    r.pos = lerp(a.pos, b.pos, t);
    r.vel = lerp(a.vel, b.vel, t);
    r.heading = lerpAngle(a.heading, b.heading, t);
    if (a.mounted() && b.mounted()) {
        r.mountPos = lerp(a.mountPos, b.mountPos, t);
        r.mountVel = lerp(a.mountVel, b.mountVel, t);
        r.mountHeading = lerpAngle(a.mountHeading, b.mountHeading, t);
    }
    return r;
}

PlayerState extrapolate(const PlayerState& s, float seconds) {
    PlayerState r = s;
    r.pos = s.pos + s.vel * seconds;
    if (s.mounted()) r.mountPos = s.mountPos + s.mountVel * seconds;
    return r;
}

MoveState classifySpeed(float speedMps, bool mounted) {
    if (mounted) {
        if (speedMps < 0.5f) return MoveState::Idle;
        if (speedMps < 3.0f) return MoveState::Walk;
        if (speedMps < 8.5f) return MoveState::Run;
        return MoveState::Sprint;
    }
    if (speedMps < 0.4f) return MoveState::Idle;
    if (speedMps < 2.4f) return MoveState::Walk;
    if (speedMps < 4.8f) return MoveState::Run;
    return MoveState::Sprint;
}

}  // namespace coop
