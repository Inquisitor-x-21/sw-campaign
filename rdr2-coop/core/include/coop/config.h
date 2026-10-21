// RDR2Coop.ini parsing. Lives in the portable core so it can be unit tested;
// unknown keys and out-of-range values produce warnings instead of failing.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace coop {

enum class MovementMode : uint8_t {
    Hybrid,    // movement tasks for animation + small corrections (looks right)
    Teleport,  // set position every frame (always in sync, slides without animation)
};

// Who gets the movement task while a stand-in rides (Hybrid only).
enum class MountedTask : uint8_t {
    Rider,     // TASK_GO_STRAIGHT_TO_COORD on the rider: didn't move the horse in game
    Horse,     // TASK_GO_STRAIGHT_TO_COORD on the horse
    AnyMeans,  // TASK_GO_TO_COORD_ANY_MEANS on the rider (rides the mount there)
};

struct Config {
    // [Network]
    uint16_t hostPort = 7777;
    std::string joinAddress = "127.0.0.1";
    uint16_t joinPort = 7777;
    int maxPlayers = 4;
    uint32_t sendRateHz = 20;

    // [Player]
    std::string name;  // empty = "Player <id>"
    std::string model = "g_m_m_uniduster_01";
    int outfit = 0;    // outfit preset, -1 = random per viewer
    std::string fallbackHorse = "a_c_horse_kentuckysaddle_black";

    // [Sync]
    uint32_t interpDelayMs = 100;
    uint32_t maxExtrapolationMs = 250;
    float teleportDistance = 25.f;
    MovementMode onFootMovement = MovementMode::Hybrid;
    MovementMode mountedMovement = MovementMode::Hybrid;
    bool clockSync = true;
    bool teleportOnJoin = true;

    // [Tuning] (Hybrid movement)
    float correctionStart = 1.0f;   // metres of error before nudging back on track
    float correctionGain = 1.5f;    // extra m/s per metre of error
    uint32_t retaskMs = 400;        // how often a movement task is refreshed
    float lookaheadSeconds = 0.5f;  // how far ahead of the remote position to aim
    MountedTask mountedTask = MountedTask::AnyMeans;

    // [Display]
    bool overlay = true;
    bool blips = true;
    std::string blipStyle = "BLIP_STYLE_MP_PLAYER";

    // [Hotkeys] (Windows virtual-key codes; 0 = disabled)
    int keyHost = 0x76;     // F7
    int keyJoin = 0x77;     // F8
    int keyLeave = 0x78;    // F9
    int keyOverlay = 0x79;  // F10
    int keyRegroup = 0x7A;  // F11
    int keyReload = 0x75;   // F6

    // [Debug]
    bool verboseLog = false;
    uint32_t statsIntervalMs = 10000;  // 0 = off
};

bool parseConfig(const std::string& text, Config& cfg, std::vector<std::string>& warnings);
// Returns false if the file can't be read (cfg keeps its defaults).
bool loadConfigFile(const std::string& path, Config& cfg, std::vector<std::string>& warnings);

int parseKeyName(const std::string& name);  // virtual-key code, 0 for NONE, -1 if unknown
std::string keyName(int vk);
const char* toString(MovementMode m);
const char* toString(MountedTask t);
std::string describeConfig(const Config& cfg);

}  // namespace coop
