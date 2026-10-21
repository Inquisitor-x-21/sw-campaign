// Small game-facing helpers over the generated natives. Everything here must
// run on Script Hook's script fiber.
#pragma once

#include <cstdint>
#include <string>

#include "coop/protocol.h"
#include "natives_extra.h"

namespace game {

using coop::Vec3;

inline Vec3 toVec(const rdr::NVec3& v) { return {v.x, v.y, v.z}; }

struct LocalCapture {
    bool ok = false;
    rdr::Ped ped = 0;
    rdr::Ped mount = 0;
    coop::PlayerState state;  // timeMs and seq are filled in by the session
};

LocalCapture captureLocalPlayer();
bool isLoadingOrFaded();

coop::WorldState readClock();
void setClock(const coop::WorldState& w);
int secondsOfDay(const coop::WorldState& w);

// Moves the local player (and their horse, if riding) to pos.
void teleportLocalPlayer(Vec3 pos, float heading);

bool modelExists(uint32_t modelHash);
// 1 = loaded, 0 = still loading, -1 = missing or timed out.
struct ModelRequest {
    uint32_t hash = 0;
    int64_t since = 0;
    bool active = false;
};
int ensureModel(ModelRequest& req, uint32_t modelHash, int64_t nowMs);
void releaseModel(uint32_t modelHash);

rdr::Ped spawnPed(uint32_t modelHash, Vec3 pos, float heading);
// Makes a spawned ped behave like another player's stand-in: persistent,
// unkillable, ignores events, friendly to the local player.
void prepareAvatarPed(rdr::Ped ped, int outfit, bool isHorse);
void deleteEntity(rdr::Entity& entity);  // zeroes the handle
bool entityExists(rdr::Entity entity);
Vec3 entityPos(rdr::Entity entity);
float entityHeading(rdr::Entity entity);

int addBlip(rdr::Entity entity, const std::string& style, const std::string& name);
void removeBlip(int& blip);

void drawText(float x, float y, const char* text, float scale, int r, int g, int b, int a = 255);

}  // namespace game
