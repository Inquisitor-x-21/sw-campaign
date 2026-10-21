#include "game.h"

#include <cstdlib>

#include "coop/interp.h"
#include "coop/log.h"

using namespace rdr;

namespace game {
namespace {

constexpr int kRelationshipCompanion = 0;

uint32_t coopRelationshipGroup() {
    static Hash group = 0;
    if (group == 0) {
        Hash created = 0;
        PED::ADD_RELATIONSHIP_GROUP("RDR2COOP_PLAYERS", &created);
        group = created;
        const Hash player = coop::joaat("PLAYER");
        PED::SET_RELATIONSHIP_BETWEEN_GROUPS(kRelationshipCompanion, group, player);
        PED::SET_RELATIONSHIP_BETWEEN_GROUPS(kRelationshipCompanion, player, group);
        LOG_DEBUG("relationship group RDR2COOP_PLAYERS = 0x%08X", group);
    }
    return group;
}

}  // namespace

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
    if (STREAMING::HAS_MODEL_LOADED(modelHash)) return 1;
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

Vec3 entityPos(Entity entity) { return toVec(ENTITY::GET_ENTITY_COORDS(entity, TRUE, TRUE)); }

float entityHeading(Entity entity) { return coop::wrapDegrees(ENTITY::GET_ENTITY_HEADING(entity)); }

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
