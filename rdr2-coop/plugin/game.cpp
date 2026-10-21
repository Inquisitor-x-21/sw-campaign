#include "game.h"

#include <cmath>
#include <cstdlib>

#include "coop/interp.h"
#include "coop/log.h"

using namespace rdr;

namespace game {
namespace {

constexpr int kRelationshipCompanion = 0;
constexpr const char* kRelationshipGroupName = "RDR2COOP_PLAYERS";

Hash g_group = 0;

uint32_t coopRelationshipGroup() {
    if (g_group == 0) {
        Hash created = 0;
        PED::ADD_RELATIONSHIP_GROUP(kRelationshipGroupName, &created);
        // Group hashes are the name's hash; covers the group already existing.
        const Hash group = created != 0 ? created : coop::joaat(kRelationshipGroupName);
        g_group = group;
        const Hash player = coop::joaat("PLAYER");
        PED::SET_RELATIONSHIP_BETWEEN_GROUPS(kRelationshipCompanion, group, player);
        PED::SET_RELATIONSHIP_BETWEEN_GROUPS(kRelationshipCompanion, player, group);
        LOG_DEBUG("relationship group RDR2COOP_PLAYERS = 0x%08X", group);
    }
    return g_group;
}

}  // namespace

void forgetCachedHandles() { g_group = 0; }

LocalCapture captureLocalPlayer() {
    LocalCapture c;
    const Ped ped = PLAYER::PLAYER_PED_ID();
    if (ped == 0 || !ENTITY::DOES_ENTITY_EXIST(ped)) return c;
    c.ped = ped;

    coop::PlayerState& s = c.state;
    s.pos = toVec(ENTITY::GET_ENTITY_COORDS(ped, TRUE, TRUE));
    s.vel = toVec(ENTITY::GET_ENTITY_VELOCITY(ped, 0));
    s.heading = coop::wrapDegrees(ENTITY::GET_ENTITY_HEADING(ped));
    if (ENTITY::IS_ENTITY_DEAD(ped)) s.flags |= coop::StateFlag::Dead;

    if (PED::IS_PED_ON_MOUNT(ped)) {
        const Ped mount = PED::GET_MOUNT(ped);
        if (mount != 0 && ENTITY::DOES_ENTITY_EXIST(mount)) {
            c.mount = mount;
            s.flags |= coop::StateFlag::Mounted;
            s.mountModel = ENTITY::GET_ENTITY_MODEL(mount);
            s.mountPos = toVec(ENTITY::GET_ENTITY_COORDS(mount, TRUE, TRUE));
            s.mountVel = toVec(ENTITY::GET_ENTITY_VELOCITY(mount, 0));
            s.mountHeading = coop::wrapDegrees(ENTITY::GET_ENTITY_HEADING(mount));
        }
    }
    const Vec3 v = s.mounted() ? s.mountVel : s.vel;
    s.move = coop::classifySpeed(coop::length2d(v), s.mounted());
    c.ok = coop::isFinite(s.pos) && coop::isFinite(s.vel) && coop::isFinite(s.mountPos);
    return c;
}

bool isLoadingOrFaded() { return DLC::GET_IS_LOADING_SCREEN_ACTIVE() || CAMERA::IS_SCREEN_FADED_OUT(); }

coop::WorldState readClock() {
    coop::WorldState w;
    w.hour = static_cast<uint8_t>(CLOCK::GET_CLOCK_HOURS() % 24);
    w.minute = static_cast<uint8_t>(CLOCK::GET_CLOCK_MINUTES() % 60);
    w.second = static_cast<uint8_t>(CLOCK::GET_CLOCK_SECONDS() % 60);
    return w;
}

void setClock(const coop::WorldState& w) { CLOCK::SET_CLOCK_TIME(w.hour, w.minute, w.second); }

int secondsOfDay(const coop::WorldState& w) { return w.hour * 3600 + w.minute * 60 + w.second; }

void teleportLocalPlayer(Vec3 pos, float heading) {
    const Ped ped = PLAYER::PLAYER_PED_ID();
    Entity target = ped;
    if (PED::IS_PED_ON_MOUNT(ped)) {
        const Ped mount = PED::GET_MOUNT(ped);
        if (mount != 0) target = mount;
    }
    STREAMING::REQUEST_COLLISION_AT_COORD(pos.x, pos.y, pos.z);
    ENTITY::SET_ENTITY_COORDS(target, pos.x, pos.y, pos.z, FALSE, FALSE, FALSE, FALSE);
    ENTITY::SET_ENTITY_HEADING(target, heading);
}

bool modelExists(uint32_t modelHash) {
    return modelHash != 0 && STREAMING::IS_MODEL_IN_CDIMAGE(modelHash) && STREAMING::IS_MODEL_VALID(modelHash);
}

int ensureModel(ModelRequest& req, uint32_t modelHash, int64_t nowMs) {
    if (!req.active || req.hash != modelHash) {
        if (!modelExists(modelHash)) return -1;
        req.hash = modelHash;
        req.since = nowMs;
        req.active = true;
    }
    if (STREAMING::HAS_MODEL_LOADED(modelHash)) {
        // Done. A later call (a respawn after the game evicted the model) must
        // start a new request with a fresh timeout, not inherit this one's.
        req.active = false;
        return 1;
    }
    if (nowMs - req.since > 8000) {
        req.active = false;
        return -1;
    }
    STREAMING::REQUEST_MODEL(modelHash, FALSE);
    return 0;
}

void releaseModel(uint32_t modelHash) {
    if (modelHash != 0) STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(modelHash);
}

Ped spawnPed(uint32_t modelHash, Vec3 pos, float heading) {
    const Ped ped = PED::CREATE_PED(modelHash, pos.x, pos.y, pos.z, heading, FALSE, FALSE, FALSE, FALSE);
    return (ped != 0 && ENTITY::DOES_ENTITY_EXIST(ped)) ? ped : 0;
}

void prepareAvatarPed(Ped ped, int outfit, bool isHorse) {
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped, TRUE, TRUE);
    // Peds created by script are invisible until an outfit is applied.
    PED::_SET_RANDOM_OUTFIT_VARIATION(ped, TRUE);
    if (!isHorse && outfit >= 0) PED::_EQUIP_META_PED_OUTFIT_PRESET(ped, outfit, FALSE);
    ENTITY::SET_ENTITY_INVINCIBLE(ped, TRUE);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, TRUE);
    PED::SET_PED_KEEP_TASK(ped, TRUE);
    PED::SET_PED_CAN_RAGDOLL(ped, FALSE);
    PED::SET_PED_CAN_BE_TARGETTED(ped, FALSE);
    PED::SET_PED_FLEE_ATTRIBUTES(ped, 0, FALSE);
    PED::SET_PED_RELATIONSHIP_GROUP_HASH(ped, coopRelationshipGroup());
}

void deleteEntity(Entity& entity) {
    if (entity != 0 && ENTITY::DOES_ENTITY_EXIST(entity)) {
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(entity, TRUE, TRUE);
        ENTITY::DELETE_ENTITY(&entity);
    }
    entity = 0;
}

bool entityExists(Entity entity) { return entity != 0 && ENTITY::DOES_ENTITY_EXIST(entity); }

bool entityHasModel(Entity entity, uint32_t modelHash) {
    return entityExists(entity) && ENTITY::GET_ENTITY_MODEL(entity) == modelHash;
}

Vec3 entityPos(Entity entity) { return toVec(ENTITY::GET_ENTITY_COORDS(entity, TRUE, TRUE)); }

float entityHeading(Entity entity) { return coop::wrapDegrees(ENTITY::GET_ENTITY_HEADING(entity)); }

float moveBlendForSpeed(Ped mover, float speed, bool isHorse) {
    if (speed < 0.1f) return 0.f;
    const float fromGame = PED::_COMPUTE_SPEED_FOR_PED_MOVE_BLEND_RATIO(mover, speed);
    if (std::isfinite(fromGame) && fromGame > 0.f && fromGame <= 3.f) return fromGame;
    // Typical speeds (m/s) at blend 0, 1, 2 and 3.
    static const float foot[] = {0.f, 1.4f, 3.6f, 6.2f};
    static const float horse[] = {0.f, 1.8f, 5.5f, 11.f};
    const float* s = isHorse ? horse : foot;
    for (int i = 1; i < 4; ++i) {
        if (speed <= s[i]) return static_cast<float>(i - 1) + (speed - s[i - 1]) / (s[i] - s[i - 1]);
    }
    return 3.f;
}

void taskGoTo(Ped ped, Vec3 to, float moveBlend, float heading, float radius, bool anyMeans) {
    if (anyMeans) {
        TASK::TASK_GO_TO_COORD_ANY_MEANS(ped, to.x, to.y, to.z, moveBlend, 0, FALSE, 0, -1.f);
    } else {
        TASK::TASK_GO_STRAIGHT_TO_COORD(ped, to.x, to.y, to.z, moveBlend, -1, heading, radius, 0);
    }
}

int addBlip(Entity entity, const std::string& style, const std::string& name) {
    const Blip blip = MAP::BLIP_ADD_FOR_ENTITY(coop::joaat(style), entity);
    if (blip != 0 && !name.empty()) MAP::_SET_BLIP_NAME(blip, MISC::VAR_STRING_LITERAL(name.c_str()));
    return blip;
}

void removeBlip(int& blip) {
    if (blip != 0 && MAP::DOES_BLIP_EXIST(blip)) MAP::REMOVE_BLIP(&blip);
    blip = 0;
}

void drawText(float x, float y, const char* text, float scale, int r, int g, int b, int a) {
    UIDEBUG::_BG_SET_TEXT_SCALE(scale, scale);
    UIDEBUG::_BG_SET_TEXT_COLOR(r, g, b, a);
    UIDEBUG::_BG_DISPLAY_TEXT(MISC::VAR_STRING_LITERAL(text), x, y);
}

}  // namespace game
