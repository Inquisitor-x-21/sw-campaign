#include "avatar.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "coop/log.h"

using namespace rdr;
using coop::MoveState;
using coop::MovementMode;
using coop::PlayerState;
using coop::Vec3;

namespace {

constexpr const char* kFallbackRider = "g_m_m_uniduster_01";
constexpr int64_t kHorseIdleDespawnMs = 120000;
constexpr float kHorseLeftBehindDistance = 150.f;
// Hybrid: extra speed per metre of falling behind (or ahead), and its limits.
constexpr float kCatchUpPerMetre = 0.5f;
constexpr float kMaxCatchUp = 3.f;
constexpr float kMaxSlowDown = 1.5f;

// Outfit preset counts from the community ped list (femga/rdr3_discoveries).
int clampOutfit(const std::string& model, int outfit) {
    struct Known {
        const char* model;
        int outfits;
    };
    static const Known known[] = {
        {"g_m_m_uniduster_01", 184},      {"g_m_m_uniduster_02", 56},     {"g_m_m_uniduster_03", 37},
        {"g_m_m_unicriminals_01", 150},   {"g_m_m_unicriminals_02", 60},  {"g_m_m_unibanditos_01", 167},
        {"g_m_m_unimountainmen_01", 129}, {"g_m_m_uniranchers_01", 87},   {"g_m_y_uniexconfeds_01", 124},
        {"g_m_y_uniexconfeds_02", 33},    {"g_m_o_uniexconfeds_01", 93},  {"a_m_m_rancher_01", 20},
    };
    if (outfit < 0) return -1;
    std::string lower = model;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const Known& k : known) {
        if (lower == k.model) return outfit % k.outfits;
    }
    return outfit;
}

}  // namespace

AvatarTuning tuningFromConfig(const coop::Config& cfg) {
    AvatarTuning t;
    t.onFoot = cfg.onFootMovement;
    t.mounted = cfg.mountedMovement;
    t.teleportDistance = cfg.teleportDistance;
    t.correctionStart = cfg.correctionStart;
    t.correctionGain = cfg.correctionGain;
    t.retaskMs = cfg.retaskMs;
    t.lookahead = cfg.lookaheadSeconds;
    t.mountedTask = cfg.mountedTask;
    t.blips = cfg.blips;
    t.blipStyle = cfg.blipStyle;
    t.fallbackHorse = cfg.fallbackHorse;
    return t;
}

RemoteAvatar::RemoteAvatar(const coop::PlayerInfo& info) : info_(info) {}

RemoteAvatar::~RemoteAvatar() { despawn(); }

void RemoteAvatar::despawn() {
    game::removeBlip(blip_);
    game::deleteEntity(ped_);
    game::deleteEntity(horse_);
    footDrive_ = {};
    horseDrive_ = {};
    horseIdleSince_ = -1;
}

void RemoteAvatar::refreshBlip(const AvatarTuning& tuning) {
    game::removeBlip(blip_);
    blipStyle_ = tuning.blipStyle;
    blipsOn_ = tuning.blips;
    if (tuning.blips && ped_ != 0) {
        blip_ = game::addBlip(ped_, tuning.blipStyle, info_.name);
        // Not retried every frame: a style the game doesn't know fails each time.
        if (blip_ == 0) {
            LOG_WARN("couldn't add a %s blip for %s; try another BlipStyle", tuning.blipStyle.c_str(),
                     info_.name.c_str());
        }
    }
}

bool RemoteAvatar::spawnRider(const PlayerState& s, int64_t now, const AvatarTuning& tuning) {
    if (riderModel_ == 0) {
        riderModel_ = coop::joaat(info_.model.empty() ? kFallbackRider : info_.model);
        if (!game::modelExists(riderModel_)) {
            LOG_WARN("%s wants model '%s', which this game doesn't have; using %s", info_.name.c_str(),
                     info_.model.c_str(), kFallbackRider);
            riderModel_ = coop::joaat(kFallbackRider);
            riderOnFallback_ = true;
        }
    }
    const int loaded = game::ensureModel(riderReq_, riderModel_, now);
    if (loaded == 0) {
        status_ = "loading model";
        return false;
    }
    if (loaded < 0) {
        if (!riderOnFallback_) {
            LOG_WARN("model for %s didn't load; trying %s", info_.name.c_str(), kFallbackRider);
            riderModel_ = coop::joaat(kFallbackRider);
            riderOnFallback_ = true;
        } else {
            retryAt_ = now + 5000;
            status_ = "can't load model";
        }
        return false;
    }

    const Vec3 pos = s.mounted() ? s.mountPos : s.pos;
    const float heading = s.mounted() ? s.mountHeading : s.heading;
    ped_ = game::spawnPed(riderModel_, pos, heading);
    game::releaseModel(riderModel_);
    if (ped_ == 0) {
        retryAt_ = now + 2000;
        status_ = "spawn failed, retrying";
        LOG_WARN("CREATE_PED failed for %s, retrying in 2 s", info_.name.c_str());
        return false;
    }
    const int outfit = riderOnFallback_ ? 0 : clampOutfit(info_.model, info_.outfit);
    game::prepareAvatarPed(ped_, outfit, false);
    refreshBlip(tuning);
    footDrive_ = {};
    horseDrive_ = {};
    status_ = riderOnFallback_ ? "ok (fallback model)" : "ok";
    LOG_INFO("spawned %s as %s outfit %d (ped %d) at %.1f %.1f %.1f", info_.name.c_str(),
             riderOnFallback_ ? kFallbackRider : info_.model.c_str(), outfit, ped_, pos.x, pos.y, pos.z);
    return true;
}

bool RemoteAvatar::ensureHorse(const PlayerState& s, int64_t now, const AvatarTuning& tuning) {
    const uint32_t fallback = coop::joaat(tuning.fallbackHorse);
    const uint32_t want = s.mountModel != 0 ? s.mountModel : fallback;

    if (horse_ != 0 && !game::entityExists(horse_)) horse_ = 0;
    if (horse_ != 0 && want != horseWanted_) {
        LOG_INFO("%s switched horses", info_.name.c_str());
        game::deleteEntity(horse_);
        horseDrive_ = {};
    }
    if (horse_ != 0) return true;
    if (now < horseRetryAt_) return false;

    if (want != horseWanted_) {
        horseWanted_ = want;
        horseModel_ = game::modelExists(want) ? want : fallback;
        horseReq_ = {};
    }
    const int loaded = game::ensureModel(horseReq_, horseModel_, now);
    if (loaded == 0) {
        status_ = "loading horse";
        return false;
    }
    if (loaded < 0) {
        if (horseModel_ != fallback) {
            LOG_WARN("horse 0x%08X for %s didn't load; using %s", horseModel_, info_.name.c_str(),
                     tuning.fallbackHorse.c_str());
            horseModel_ = fallback;
            horseReq_ = {};
        } else {
            horseRetryAt_ = now + 5000;
            status_ = "can't load horse";
        }
        return false;
    }

    horse_ = game::spawnPed(horseModel_, s.mountPos, s.mountHeading);
    game::releaseModel(horseModel_);
    if (horse_ == 0) {
        horseRetryAt_ = now + 2000;
        LOG_WARN("CREATE_PED failed for %s's horse, retrying in 2 s", info_.name.c_str());
        return false;
    }
    game::prepareAvatarPed(horse_, -1, true);
    horseDrive_ = {};
    status_ = "ok";
    LOG_INFO("spawned horse 0x%08X for %s%s", horseModel_, info_.name.c_str(),
             horseModel_ == want ? "" : " (fallback breed)");
    return true;
}

void RemoteAvatar::dismountIfNeeded(const PlayerState& s) {
    const bool onMount = horse_ != 0 && PED::IS_PED_ON_MOUNT(ped_);
    noteSeatChange(onMount, "CLEAR_PED_TASKS_IMMEDIATELY didn't dismount the rider");
    if (!onMount) return;
    TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped_, TRUE, TRUE);
    ENTITY::SET_ENTITY_COORDS_NO_OFFSET(ped_, s.pos.x, s.pos.y, s.pos.z, FALSE, FALSE, FALSE);
    footDrive_ = {};
    horseDrive_ = {};
}

// Mounting and dismounting are expected to take effect at once. If one keeps
// being retried, say so once in the log: it means the native doesn't do what
// we assume, which nobody can see from outside the game.
void RemoteAvatar::noteSeatChange(bool retrying, const char* problem) {
    constexpr uint32_t kWarnAfterFrames = 60;
    if (!retrying) {
        seatRetries_ = 0;
        return;
    }
    if (++seatRetries_ == kWarnAfterFrames) {
        LOG_WARN("%s: %s after %u frames (still retrying)", info_.name.c_str(), problem, kWarnAfterFrames);
    }
}

// Once per avatar and gait type, log what the game says about speed: whether
// _COMPUTE_SPEED_FOR_PED_MOVE_BLEND_RATIO answers sensibly decides how well
// stand-ins match speeds, and nobody can see that from outside the game.
void RemoteAvatar::logMoveBlendOnce(const Mover& m) {
    bool& done = m.isHorse ? loggedHorseBlend_ : loggedFootBlend_;
    if (done) return;
    done = true;
    const float fast = m.isHorse ? 11.f : 6.f;
    LOG_INFO("%s %s: move blend from the game for 1.5 / 3.5 / %.0f m/s = %.2f / %.2f / %.2f (used: %.2f / %.2f / %.2f)",
             info_.name.c_str(), m.isHorse ? "horse" : "on foot", fast,
             PED::_COMPUTE_SPEED_FOR_PED_MOVE_BLEND_RATIO(m.entity, 1.5f),
             PED::_COMPUTE_SPEED_FOR_PED_MOVE_BLEND_RATIO(m.entity, 3.5f),
             PED::_COMPUTE_SPEED_FOR_PED_MOVE_BLEND_RATIO(m.entity, fast),
             game::moveBlendForSpeed(m.entity, 1.5f, m.isHorse), game::moveBlendForSpeed(m.entity, 3.5f, m.isHorse),
             game::moveBlendForSpeed(m.entity, fast, m.isHorse));
}

void RemoteAvatar::forgetEntities() {
    ped_ = 0;
    horse_ = 0;
    blip_ = 0;
    horseWanted_ = 0;
    riderReq_ = {};
    horseReq_ = {};
    retryAt_ = 0;
    horseRetryAt_ = 0;
    horseIdleSince_ = -1;
    seatRetries_ = 0;
    footDrive_ = {};
    horseDrive_ = {};
    status_ = "waiting for position";
}

void RemoteAvatar::maybeDropIdleHorse(int64_t now) {
    if (horse_ == 0) return;
    if (horseIdleSince_ < 0) horseIdleSince_ = now;
    const bool idleTooLong = now - horseIdleSince_ > kHorseIdleDespawnMs;
    const bool leftBehind = coop::distance(game::entityPos(horse_), game::entityPos(ped_)) > kHorseLeftBehindDistance;
    if (idleTooLong || leftBehind) {
        game::deleteEntity(horse_);
        horseWanted_ = 0;
        horseIdleSince_ = -1;
    }
}

void RemoteAvatar::update(const coop::SampleResult& sample, int64_t now, const AvatarTuning& tuning) {
    const PlayerState& s = sample.state;
    if (sample.extrapolated) ++stats_.extrapolated;
    stats_.minBufferMs = std::min(stats_.minBufferMs, sample.bufferAheadMs);

    if (horse_ != 0 && !game::entityHasModel(horse_, horseModel_)) horse_ = 0;
    if (ped_ != 0 && !game::entityHasModel(ped_, riderModel_)) {
        LOG_WARN("stand-in for %s disappeared (game cleanup?), respawning", info_.name.c_str());
        ped_ = 0;
        game::removeBlip(blip_);
        game::deleteEntity(horse_);
    }
    if (ped_ == 0 && (now < retryAt_ || !spawnRider(s, now, tuning))) return;
    if (tuning.blips != blipsOn_ || blipStyle_ != tuning.blipStyle) refreshBlip(tuning);

    if (s.mounted()) {
        lastTarget_ = s.mountPos;
        if (ensureHorse(s, now, tuning)) {
            horseIdleSince_ = -1;
            const bool seated = PED::IS_PED_ON_MOUNT(ped_) && PED::GET_MOUNT(ped_) == horse_;
            if (!seated) {
                PED::SET_PED_ONTO_MOUNT(ped_, horse_, -1, TRUE);
                horseDrive_ = {};
            }
            noteSeatChange(!seated, "SET_PED_ONTO_MOUNT didn't seat the rider");
            if (!loggedMountedTask_ || tuning.mountedTask != mountedTask_) {
                LOG_INFO("%s rides with MountedTask = %s", info_.name.c_str(), coop::toString(tuning.mountedTask));
                loggedMountedTask_ = true;
                mountedTask_ = tuning.mountedTask;
                horseDrive_ = {};  // task the new ped right away
            }
            const Mover horse{horse_, tuning.mountedTask == coop::MountedTask::Horse ? horse_ : ped_, true,
                              tuning.mountedTask == coop::MountedTask::AnyMeans};
            drive(horse, s.mountPos, s.mountVel, s.mountHeading, s.move, tuning.mounted, horseDrive_, now, tuning);
        } else {
            // Horse still loading: keep the rider where the horse will appear.
            drive(Mover{ped_, ped_, false, false}, s.mountPos, s.mountVel, s.mountHeading, s.move,
                  MovementMode::Teleport, footDrive_, now, tuning);
        }
        return;
    }

    dismountIfNeeded(s);
    maybeDropIdleHorse(now);
    lastTarget_ = s.pos;
    drive(Mover{ped_, ped_, false, false}, s.pos, s.vel, s.heading, s.move, tuning.onFoot, footDrive_, now, tuning);
}

void RemoteAvatar::drive(const Mover& m, Vec3 target, Vec3 vel, float heading, MoveState move, MovementMode mode,
                         Drive& d, int64_t now, const AvatarTuning& t) {
    const Entity entity = m.entity;
    const Vec3 current = game::entityPos(entity);
    const Vec3 error = target - current;
    const float dist = coop::length(error);
    stats_.errorSum += dist;
    stats_.errorMax = std::max(stats_.errorMax, dist);
    ++stats_.samples;

    if (mode == MovementMode::Teleport || dist > t.teleportDistance) {
        ENTITY::SET_ENTITY_COORDS_NO_OFFSET(entity, target.x, target.y, target.z, FALSE, FALSE, FALSE);
        ENTITY::SET_ENTITY_HEADING(entity, heading);
        if (mode == MovementMode::Teleport) {
            ENTITY::SET_ENTITY_VELOCITY(entity, vel.x, vel.y, vel.z);
        } else {
            ++stats_.warps;
            d = {};
        }
        return;
    }

    // Hybrid: a movement task gives real walk / run / ride animations; a
    // velocity nudge pulls the stand-in back when it drifts off the real path.
    if (move != MoveState::Idle) {
        logMoveBlendOnce(m);
        // The remote player's exact speed, not just walk / run / sprint, plus
        // a little more (or less) to close a gap along the way they're going.
        const float speed = coop::length2d(vel);
        const Vec3 dir = speed > 0.1f ? vel * (1.f / speed) : Vec3{};
        const float behind = error.x * dir.x + error.y * dir.y;
        const float catchUp = std::clamp(behind * kCatchUpPerMetre, -kMaxSlowDown, kMaxCatchUp);
        const float blend = game::moveBlendForSpeed(entity, std::max(0.f, speed + catchUp), m.isHorse);

        const Vec3 aim = target + vel * t.lookahead;
        const bool stale = now - d.lastTaskAt > static_cast<int64_t>(t.retaskMs);
        // Restarting a task interrupts its animation, so at speed only re-aim
        // about twice a second; speed changes go through the desired blend.
        const float retaskDistance = std::max(1.5f, speed * 0.5f);
        if (!d.tasked || move != d.lastMove || coop::distance(aim, d.lastTaskTarget) > retaskDistance || stale) {
            game::taskGoTo(m.taskPed, aim, blend, heading, 0.5f, m.anyMeans);
            d.tasked = true;
            d.lastMove = move;
            d.lastTaskTarget = aim;
            d.lastTaskAt = now;
        } else {
            TASK::SET_PED_DESIRED_MOVE_BLEND_RATIO(m.taskPed, blend);
        }
        if (dist > t.correctionStart) {
            const Vec3 nudge = vel + error * t.correctionGain;
            ENTITY::SET_ENTITY_VELOCITY(entity, nudge.x, nudge.y, vel.z);
        }
        return;
    }

    // Standing still: walk onto the exact spot, face the same way, stop.
    const bool justStopped = !d.tasked || d.lastMove != MoveState::Idle;
    const bool drifted = dist > 0.75f && now - d.lastTaskAt > static_cast<int64_t>(t.retaskMs);
    if (justStopped || drifted) {
        game::taskGoTo(m.taskPed, target, 1.f, heading, 0.25f, m.anyMeans);
        d.tasked = true;
        d.lastMove = MoveState::Idle;
        d.lastTaskTarget = target;
        d.lastTaskAt = now;
    } else if (dist <= 0.75f && std::fabs(coop::angleDelta(game::entityHeading(entity), heading)) > 25.f) {
        ENTITY::SET_ENTITY_HEADING(entity, heading);
    }
}
