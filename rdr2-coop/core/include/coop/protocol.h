// Wire protocol shared by the game plugin and the test bots.
//
// Session layout: one player hosts, up to three others connect. Clients only
// talk to the host; the host relays each player's state to everyone else.
// Channel 0 carries reliable control messages (join, leave, world), channel 1
// carries unreliable player states (newer states replace lost ones).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "coop/mathutil.h"

namespace coop {

constexpr uint16_t kProtocolVersion = 1;
constexpr int kMaxPlayersLimit = 4;  // hard cap, host included

constexpr uint8_t kChannelControl = 0;  // reliable, ordered
constexpr uint8_t kChannelState = 1;    // unreliable, sequenced
constexpr uint8_t kChannelCount = 2;

constexpr size_t kMaxNameLen = 24;
constexpr size_t kMaxModelLen = 48;
constexpr size_t kMaxVersionLen = 16;
constexpr size_t kMaxDetailLen = 96;

using PlayerId = uint8_t;
constexpr PlayerId kNoPlayer = 0;
constexpr PlayerId kHostId = 1;  // clients get 2..maxPlayers

enum class MsgType : uint8_t {
    Hello = 1,         // client -> host: who I am
    Welcome = 2,       // host -> client: your id, who is here, world state
    Reject = 3,        // host -> client: refused (full, wrong version)
    PlayerJoined = 4,  // host -> clients
    PlayerLeft = 5,    // host -> clients
    PlayerState = 6,   // client -> host, and host -> clients (relayed)
    WorldState = 7,    // host -> clients: clock
};

enum class RejectReason : uint8_t { VersionMismatch = 1, SessionFull = 2, BadHello = 3 };
enum class LeaveReason : uint8_t { Quit = 0, TimedOut = 1 };

enum class MoveState : uint8_t { Idle = 0, Walk = 1, Run = 2, Sprint = 3 };

namespace StateFlag {
constexpr uint8_t Mounted = 1 << 0;
constexpr uint8_t Dead = 1 << 1;
constexpr uint8_t Teleported = 1 << 2;  // position jumped (fast travel, cutscene, respawn)
}  // namespace StateFlag

struct PlayerState {
    uint32_t timeMs = 0;  // sender's clock when sampled
    uint16_t seq = 0;
    Vec3 pos;             // the player's own coordinates
    Vec3 vel;
    float heading = 0.f;
    MoveState move = MoveState::Idle;
    uint8_t flags = 0;
    // Only sent while flags has Mounted.
    uint32_t mountModel = 0;  // model hash of the horse
    Vec3 mountPos;
    Vec3 mountVel;
    float mountHeading = 0.f;

    bool mounted() const { return (flags & StateFlag::Mounted) != 0; }
};

struct PlayerInfo {
    PlayerId id = kNoPlayer;
    std::string name;
    std::string model;   // ped model name the player appears as
    int16_t outfit = -1; // outfit preset index, -1 = random
};

struct WorldState {
    uint8_t hour = 12;
    uint8_t minute = 0;
    uint8_t second = 0;
};

struct HelloMsg {
    uint16_t protocol = kProtocolVersion;
    std::string modVersion;
    PlayerInfo info;  // id ignored
};

struct WelcomeMsg {
    PlayerId yourId = kNoPlayer;
    uint8_t maxPlayers = kMaxPlayersLimit;
    WorldState world;
    std::vector<PlayerInfo> players;  // everyone already here, host included
};

struct RejectMsg {
    RejectReason reason = RejectReason::BadHello;
    std::string detail;
};

struct PlayerJoinedMsg { PlayerInfo info; };
struct PlayerLeftMsg { PlayerId id = kNoPlayer; LeaveReason reason = LeaveReason::Quit; };
struct PlayerStateMsg { PlayerId id = kNoPlayer; PlayerState state; };
struct WorldStateMsg { WorldState world; };

// Encoders: the first byte of every message is its MsgType.
std::vector<uint8_t> encode(const HelloMsg& m);
std::vector<uint8_t> encode(const WelcomeMsg& m);
std::vector<uint8_t> encode(const RejectMsg& m);
std::vector<uint8_t> encode(const PlayerJoinedMsg& m);
std::vector<uint8_t> encode(const PlayerLeftMsg& m);
std::vector<uint8_t> encode(const PlayerStateMsg& m);
std::vector<uint8_t> encode(const WorldStateMsg& m);

bool peekType(const uint8_t* data, size_t size, MsgType& out);

// Decoders check the type byte, bounds, string limits and that every float is
// finite. They return false for anything malformed.
bool decode(const uint8_t* data, size_t size, HelloMsg& out);
bool decode(const uint8_t* data, size_t size, WelcomeMsg& out);
bool decode(const uint8_t* data, size_t size, RejectMsg& out);
bool decode(const uint8_t* data, size_t size, PlayerJoinedMsg& out);
bool decode(const uint8_t* data, size_t size, PlayerLeftMsg& out);
bool decode(const uint8_t* data, size_t size, PlayerStateMsg& out);
bool decode(const uint8_t* data, size_t size, WorldStateMsg& out);

// Keeps printable ASCII, trims spaces, and cuts to maxLen.
std::string sanitizeText(const std::string& s, size_t maxLen);
inline std::string sanitizeName(const std::string& s) { return sanitizeText(s, kMaxNameLen); }
// Keeps [A-Za-z0-9_] and cuts to kMaxModelLen.
std::string sanitizeModel(const std::string& s);

// RAGE's string hash (Jenkins one-at-a-time on the lowercased string), the
// same value GET_HASH_KEY returns.
uint32_t joaat(const std::string& s);

const char* toString(RejectReason r);
const char* toString(MoveState m);

}  // namespace coop
