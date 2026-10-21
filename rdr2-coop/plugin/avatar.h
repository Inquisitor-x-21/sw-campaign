// The stand-in for one remote player: a ped (and horse when they ride) that
// follows the interpolated network state.
#pragma once

#include <climits>
#include <cstdint>
#include <string>

#include "coop/config.h"
#include "coop/interp.h"
#include "game.h"

// Settings that can change on Reload Config.
struct AvatarTuning {
    coop::MovementMode onFoot = coop::MovementMode::Hybrid;
    coop::MovementMode mounted = coop::MovementMode::Hybrid;
    float teleportDistance = 25.f;
    float correctionStart = 1.f;
    float correctionGain = 1.5f;
    uint32_t retaskMs = 400;
    float lookahead = 2.f;
    coop::MountedTask mountedTask = coop::MountedTask::AnyMeans;
    bool blips = true;
    std::string blipStyle = "BLIP_STYLE_MP_PLAYER";
    std::string fallbackHorse = "a_c_horse_kentuckysaddle_black";
};

AvatarTuning tuningFromConfig(const coop::Config& cfg);

// Accumulated between stats log lines.
struct SyncStats {
    double errorSum = 0;
    float errorMax = 0;
    uint32_t samples = 0;
    uint32_t extrapolated = 0;
    uint32_t warps = 0;
    int32_t minBufferMs = INT32_MAX;
    void reset() { *this = SyncStats{}; }
};

class RemoteAvatar {
public:
    explicit RemoteAvatar(const coop::PlayerInfo& info);
    ~RemoteAvatar();
    RemoteAvatar(const RemoteAvatar&) = delete;
    RemoteAvatar& operator=(const RemoteAvatar&) = delete;

    void update(const coop::SampleResult& sample, int64_t nowMs, const AvatarTuning& tuning);
    void despawn();
    // Drops every game handle without touching the game. For after Script Hook
    // restarts the script (the game reloaded and took our peds with it).
    void forgetEntities();
    void refreshBlip(const AvatarTuning& tuning);

    const coop::PlayerInfo& info() const { return info_; }
    bool spawned() const { return ped_ != 0; }
    coop::Vec3 shownPosition() const { return lastTarget_; }
    const std::string& status() const { return status_; }
    SyncStats& stats() { return stats_; }

private:
    // What drive() moves, and how.
    struct Mover {
        rdr::Entity entity;  // has to end up at the target (the horse when riding)
        rdr::Ped taskPed;    // gets the movement task
        bool isHorse;        // entity is a horse (for speed and gait)
        bool anyMeans;       // TASK_GO_TO_COORD_ANY_MEANS instead of TASK_GO_STRAIGHT_TO_COORD
    };

    struct Drive {
        coop::MoveState lastMove = coop::MoveState::Idle;
        coop::Vec3 lastTaskTarget;
        int64_t lastTaskAt = 0;
        bool tasked = false;
    };

    bool spawnRider(const coop::PlayerState& s, int64_t nowMs, const AvatarTuning& tuning);
    bool ensureHorse(const coop::PlayerState& s, int64_t nowMs, const AvatarTuning& tuning);
    void dismountIfNeeded(const coop::PlayerState& s);
    void maybeDropIdleHorse(int64_t nowMs);
    void noteSeatChange(bool retrying, const char* problem);
    void checkMoveBlendOnce(const Mover& m);
    float moveBlend(const Mover& m, float speed) const;
    void drive(const Mover& m, coop::Vec3 target, coop::Vec3 vel, float heading, coop::MoveState move,
               coop::MovementMode mode, Drive& d, int64_t nowMs, const AvatarTuning& tuning);

    coop::PlayerInfo info_;
    uint32_t riderModel_ = 0;
    bool riderOnFallback_ = false;
    rdr::Ped ped_ = 0;
    int blip_ = 0;
    bool blipsOn_ = false;  // blips setting of the last refreshBlip
    std::string blipStyle_;
    game::ModelRequest riderReq_;
    int64_t retryAt_ = 0;

    rdr::Ped horse_ = 0;
    uint32_t horseWanted_ = 0;   // breed the remote player rides
    uint32_t horseModel_ = 0;    // breed we actually spawned (may be the fallback)
    game::ModelRequest horseReq_;
    int64_t horseRetryAt_ = 0;
    int64_t horseIdleSince_ = -1;
    uint32_t seatRetries_ = 0;
    // Per gait type: checked whether the game's speed-to-blend answers vary
    // with speed, and whether to use them.
    bool checkedFootBlend_ = false;
    bool checkedHorseBlend_ = false;
    bool gameFootBlend_ = false;
    bool gameHorseBlend_ = false;
    bool loggedMountedTask_ = false;
    coop::MountedTask mountedTask_ = coop::MountedTask::AnyMeans;

    Drive footDrive_;
    Drive horseDrive_;
    coop::Vec3 lastTarget_;
    SyncStats stats_;
    std::string status_ = "waiting for position";
};
