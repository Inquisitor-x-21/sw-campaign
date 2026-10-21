#include "fake_game.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <set>

#include "coop/protocol.h"
#include "natives_extra.h"  // hash constants; main.h resolves to tests/sim/main.h

namespace sim {
namespace {

struct World {
    std::map<int, Ent> ents;
    std::map<int, std::string> blips;
    std::map<uint32_t, bool> horseModel;   // known models -> is horse
    std::map<uint32_t, int> loading;       // model -> frames since requested
    std::set<uint32_t> loaded;
    std::set<uint32_t> released;           // no longer needed: evicted once nothing uses them
    int nextId = 100;
    int nextBlip = 5000;
    int clock = 12 * 3600;
    bool busy = false;
    std::vector<std::string> text;
    std::vector<uint64_t> unknown;
    int badPushes = 0;
    int createFailures = 0;
    int blipAdds = 0;
} w;

uint64_t g_hash = 0;
std::vector<uint64_t> g_args;
uint64_t g_result[3];

int I(size_t i) {
    if (i >= g_args.size()) {
        ++w.badPushes;
        return 0;
    }
    return static_cast<int>(static_cast<uint32_t>(g_args[i]));
}

float F(size_t i) {
    if (i >= g_args.size()) {
        ++w.badPushes;
        return 0.f;
    }
    const uint32_t bits = static_cast<uint32_t>(g_args[i]);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

template <typename T>
T* P(size_t i) {
    if (i >= g_args.size()) {
        ++w.badPushes;
        return nullptr;
    }
    return reinterpret_cast<T*>(static_cast<uintptr_t>(g_args[i]));
}

void retInt(int v) { g_result[0] = static_cast<uint32_t>(v); }
void retBool(bool v) { retInt(v ? 1 : 0); }
void retFloat(float v) { std::memcpy(&g_result[0], &v, 4); }
void retPtr(const void* p) { g_result[0] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p)); }
void retVec(coop::Vec3 v) {
    std::memcpy(&g_result[0], &v.x, 4);
    std::memcpy(&g_result[1], &v.y, 4);
    std::memcpy(&g_result[2], &v.z, 4);
}

Ent* ent(int id) {
    auto it = w.ents.find(id);
    return it == w.ents.end() ? nullptr : &it->second;
}

// Speeds (m/s) at move blend 0, 1, 2 and 3: still, walk, run, sprint/gallop.
const float kFootSpeed[] = {0.f, 1.5f, 3.5f, 6.0f};
const float kHorseSpeed[] = {0.f, 2.0f, 6.0f, 12.0f};

float speedForBlend(bool horse, float blend) {
    const float* s = horse ? kHorseSpeed : kFootSpeed;
    blend = std::clamp(blend, 0.f, 3.f);
    const int i = std::min(2, static_cast<int>(blend));
    return s[i] + (s[i + 1] - s[i]) * (blend - static_cast<float>(i));
}

float blendForSpeed(bool horse, float speed) {
    const float* s = horse ? kHorseSpeed : kFootSpeed;
    for (int i = 1; i < 4; ++i) {
        if (speed <= s[i]) return static_cast<float>(i - 1) + (speed - s[i - 1]) / (s[i] - s[i - 1]);
    }
    return 3.f;
}

void dismount(Ent& rider) {
    if (Ent* m = ent(rider.mount)) m->rider = 0;
    rider.mount = 0;
}

void dispatch() {
    using namespace rdr;
    switch (g_hash) {
        case PLAYER::PLAYER_PED_ID_HASH: retInt(1); break;
        case PLAYER::PLAYER_ID_HASH: retInt(0); break;
        case ENTITY::DOES_ENTITY_EXIST_HASH: retBool(ent(I(0)) != nullptr); break;
        case ENTITY::GET_ENTITY_COORDS_HASH: { Ent* e = ent(I(0)); I(2); retVec(e ? e->pos : coop::Vec3{}); break; }
        case ENTITY::GET_ENTITY_HEADING_HASH: { Ent* e = ent(I(0)); retFloat(e ? e->heading : 0.f); break; }
        case ENTITY::GET_ENTITY_VELOCITY_HASH: { Ent* e = ent(I(0)); I(1); retVec(e ? e->vel : coop::Vec3{}); break; }
        case ENTITY::GET_ENTITY_MODEL_HASH: { Ent* e = ent(I(0)); retInt(e ? static_cast<int>(e->model) : 0); break; }
        case ENTITY::SET_ENTITY_COORDS_HASH:
        case ENTITY::SET_ENTITY_COORDS_NO_OFFSET_HASH: {
            Ent* e = ent(I(0));
            I(6);
            if (e != nullptr) {
                if (e->mount != 0) dismount(*e);  // warping a rider knocks them off
                e->pos = {F(1), F(2), F(3)};
                if (Ent* r = ent(e->rider)) r->pos = e->pos + coop::Vec3{0, 0, 1};
            }
            break;
        }
        case ENTITY::SET_ENTITY_HEADING_HASH: if (Ent* e = ent(I(0))) e->heading = coop::wrapDegrees(F(1)); break;
        case ENTITY::SET_ENTITY_VELOCITY_HASH:
            if (Ent* e = ent(I(0))) {
                e->velOverride = true;
                e->overrideVel = {F(1), F(2), F(3)};
            }
            break;
        case ENTITY::SET_ENTITY_AS_MISSION_ENTITY_HASH: I(2); if (Ent* e = ent(I(0))) e->mission = true; break;
        case ENTITY::SET_ENTITY_INVINCIBLE_HASH: if (Ent* e = ent(I(0))) e->invincible = I(1) != 0; break;
        case ENTITY::IS_ENTITY_DEAD_HASH: I(0); retBool(false); break;
        case ENTITY::DELETE_ENTITY_HASH: {
            int* handle = P<int>(0);
            if (handle != nullptr) {
                if (Ent* e = ent(*handle)) {
                    if (!e->mission) ++w.badPushes;  // the game refuses to delete non-mission entities
                    if (e->mount != 0) dismount(*e);
                    if (Ent* r = ent(e->rider)) r->mount = 0;
                    w.ents.erase(*handle);
                }
                *handle = 0;
            }
            break;
        }
        case STREAMING::IS_MODEL_IN_CDIMAGE_HASH:
        case STREAMING::IS_MODEL_VALID_HASH: retBool(w.horseModel.count(static_cast<uint32_t>(I(0))) != 0); break;
        case STREAMING::REQUEST_MODEL_HASH: {
            const uint32_t m = static_cast<uint32_t>(I(0));
            I(1);
            if (w.horseModel.count(m) != 0 && w.loaded.count(m) == 0 && w.loading.count(m) == 0) w.loading[m] = 0;
            w.released.erase(m);
            break;
        }
        case STREAMING::HAS_MODEL_LOADED_HASH: retBool(w.loaded.count(static_cast<uint32_t>(I(0))) != 0); break;
        case STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED_HASH: w.released.insert(static_cast<uint32_t>(I(0))); break;
        case STREAMING::REQUEST_COLLISION_AT_COORD_HASH: F(2); break;
        case PED::CREATE_PED_HASH: {
            const uint32_t m = static_cast<uint32_t>(I(0));
            I(8);
            if (w.loaded.count(m) == 0) {
                ++w.createFailures;
                retInt(0);
                break;
            }
            Ent e;
            e.id = w.nextId++;
            e.model = m;
            e.isHorse = w.horseModel[m];
            e.pos = {F(1), F(2), F(3)};
            e.heading = F(4);
            w.ents[e.id] = e;
            retInt(e.id);
            break;
        }
        case PED::_SET_RANDOM_OUTFIT_VARIATION_HASH: if (Ent* e = ent(I(0))) e->visible = true; break;
        case PED::_EQUIP_META_PED_OUTFIT_PRESET_HASH: I(2); if (Ent* e = ent(I(0))) e->outfit = I(1); break;
        case PED::IS_PED_ON_MOUNT_HASH: { Ent* e = ent(I(0)); retBool(e != nullptr && e->mount != 0); break; }
        case PED::GET_MOUNT_HASH: { Ent* e = ent(I(0)); retInt(e ? e->mount : 0); break; }
        case PED::SET_PED_ONTO_MOUNT_HASH: {
            Ent* r = ent(I(0));
            Ent* m = ent(I(1));
            I(3);
            if (r != nullptr && m != nullptr && m->isHorse) {
                if (r->mount != 0) dismount(*r);
                r->mount = m->id;
                m->rider = r->id;
                r->pos = m->pos + coop::Vec3{0, 0, 1};
                r->hasTask = false;
            }
            break;
        }
        case PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS_HASH: if (Ent* e = ent(I(0))) e->blocksEvents = I(1) != 0; break;
        case PED::SET_PED_KEEP_TASK_HASH:
        case PED::SET_PED_CAN_RAGDOLL_HASH:
        case PED::SET_PED_CAN_BE_TARGETTED_HASH: I(1); break;
        case PED::SET_PED_FLEE_ATTRIBUTES_HASH: I(2); break;
        case PED::ADD_RELATIONSHIP_GROUP_HASH: {
            const char* name = P<const char>(0);
            uint32_t* out = P<uint32_t>(1);
            if (out != nullptr) *out = coop::joaat(name != nullptr ? name : "");
            retBool(true);
            break;
        }
        case PED::SET_RELATIONSHIP_BETWEEN_GROUPS_HASH: I(2); break;
        case PED::SET_PED_RELATIONSHIP_GROUP_HASH_HASH: if (Ent* e = ent(I(0))) e->relGroup = static_cast<uint32_t>(I(1)); break;
        case TASK::TASK_GO_STRAIGHT_TO_COORD_HASH: {
            Ent* e = ent(I(0));
            I(8);
            if (e != nullptr) {
                e->hasTask = true;
                e->taskAnyMeans = false;
                e->taskTarget = {F(1), F(2), F(3)};
                e->taskBlend = F(4);
                e->taskFinalHeading = F(6);
                e->taskRadius = F(7);
            }
            break;
        }
        case TASK::TASK_GO_TO_COORD_ANY_MEANS_HASH: {
            Ent* e = ent(I(0));
            F(8);
            if (e != nullptr) {
                e->hasTask = true;
                e->taskAnyMeans = true;
                e->taskTarget = {F(1), F(2), F(3)};
                e->taskBlend = F(4);
                e->taskFinalHeading = e->heading;
                e->taskRadius = 0.5f;
            }
            break;
        }
        case TASK::SET_PED_DESIRED_MOVE_BLEND_RATIO_HASH:
            if (Ent* e = ent(I(0))) e->taskBlend = F(1);
            break;
        case PED::_COMPUTE_SPEED_FOR_PED_MOVE_BLEND_RATIO_HASH: {
            // As in the game: on foot it says 2.0 for any speed right after
            // spawning, and sensible values a moment later.
            Ent* e = ent(I(0));
            F(1);
            const bool settled = e != nullptr && (e->isHorse || e->age > 30);
            retFloat(e == nullptr ? 0.f : (settled ? blendForSpeed(e->isHorse, F(1)) : 2.f));
            break;
        }
        case PED::SET_PED_MOVE_RATE_OVERRIDE_HASH:
            if (Ent* e = ent(I(0))) e->moveRate = std::clamp(F(1), 0.f, 1.15f);
            break;
        case PED::_SET_STAMINA_DEPLETION_MULTIPLIER_HASH:
            if (Ent* e = ent(I(0))) e->staminaDepletion = I(1);
            break;
        case PED::_SET_MOUNT_BONDING_LEVEL_HASH:
            if (Ent* e = ent(I(0))) e->bonding = I(1);
            break;
        case ATTRIBUTE::GET_MAX_ATTRIBUTE_RANK_HASH: I(1); retInt(10); break;
        case ATTRIBUTE::GET_ATTRIBUTE_RANK_HASH: {
            Ent* e = ent(I(0));
            const int a = I(1);
            retInt(e != nullptr && a >= 0 && a < 8 ? e->attributes[a] : 0);
            break;
        }
        case ATTRIBUTE::SET_ATTRIBUTE_BASE_RANK_HASH: {
            Ent* e = ent(I(0));
            const int a = I(1);
            if (e != nullptr && a >= 0 && a < 8) e->attributes[a] = std::clamp(I(2), 0, 10);
            break;
        }
        case TASK::TASK_STAND_STILL_HASH: if (Ent* e = ent(I(0))) e->hasTask = false; break;
        case TASK::CLEAR_PED_TASKS_IMMEDIATELY_HASH:
            I(2);
            if (Ent* e = ent(I(0))) {
                e->hasTask = false;
                if (e->mount != 0) dismount(*e);
            }
            break;
        case CLOCK::GET_CLOCK_HOURS_HASH: retInt(w.clock / 3600); break;
        case CLOCK::GET_CLOCK_MINUTES_HASH: retInt((w.clock / 60) % 60); break;
        case CLOCK::GET_CLOCK_SECONDS_HASH: retInt(w.clock % 60); break;
        case CLOCK::SET_CLOCK_TIME_HASH: w.clock = I(0) * 3600 + I(1) * 60 + I(2); break;
        case DLC::GET_IS_LOADING_SCREEN_ACTIVE_HASH: retBool(w.busy); break;
        case CAMERA::IS_SCREEN_FADED_OUT_HASH: retBool(false); break;
        case MAP::BLIP_ADD_FOR_ENTITY_HASH: {
            ++w.blipAdds;
            // A style this "game" doesn't know fails, like a typo'd BlipStyle.
            if (ent(I(1)) == nullptr || static_cast<uint32_t>(I(0)) == coop::joaat("BLIP_STYLE_MISSING")) {
                retInt(0);
                break;
            }
            const int id = w.nextBlip++;
            w.blips[id] = "";
            retInt(id);
            break;
        }
        case MAP::DOES_BLIP_EXIST_HASH: retBool(w.blips.count(I(0)) != 0); break;
        case MAP::_SET_BLIP_NAME_HASH: {
            const char* name = P<const char>(1);
            if (w.blips.count(I(0)) != 0) w.blips[I(0)] = name != nullptr ? name : "";
            break;
        }
        case MAP::REMOVE_BLIP_HASH: {
            int* handle = P<int>(0);
            if (handle != nullptr) {
                w.blips.erase(*handle);
                *handle = 0;
            }
            break;
        }
        case UIDEBUG::_BG_SET_TEXT_SCALE_HASH: F(1); break;
        case UIDEBUG::_BG_SET_TEXT_COLOR_HASH: I(3); break;
        case UIDEBUG::_BG_DISPLAY_TEXT_HASH: {
            const char* t = P<const char>(0);
            F(2);
            w.text.push_back(t != nullptr ? t : "");
            break;
        }
        case MISC::VAR_STRING_HASH: {
            if (I(0) & 1) ++w.badPushes;  // bit 0 must be clear
            const char* label = P<const char>(1);
            if (label == nullptr || std::strcmp(label, "LITERAL_STRING") != 0) ++w.badPushes;
            retPtr(P<const char>(2));
            break;
        }
        default:
            w.unknown.push_back(g_hash);
            break;
    }
}

// A stock horse (speed rank 3 of 10) gallops about 10% slower than a fully
// ranked one, like an NPC horse next to a bonded, saddled player horse.
float taskSpeed(const Ent& mover, float blend) {
    float speed = speedForBlend(mover.isHorse, std::max(1.f, blend));
    if (mover.isHorse) speed *= 0.85f + 0.15f * static_cast<float>(mover.attributes[5]) / 10.f;
    return speed * mover.moveRate;
}

}  // namespace

void reset() {
    w = World{};
    Ent player;
    player.id = 1;
    player.model = coop::joaat("player_zero");
    player.visible = true;
    w.ents[1] = player;
}

void addModel(const std::string& name, bool isHorse) { w.horseModel[coop::joaat(name)] = isHorse; }
int localPlayer() { return 1; }
Ent* get(int id) { return ent(id); }

void step(float dt) {
    // Like the game, drop released models once no entity uses them.
    for (auto it = w.released.begin(); it != w.released.end();) {
        const uint32_t m = *it;
        const bool used = std::any_of(w.ents.begin(), w.ents.end(),
                                      [m](const std::pair<const int, Ent>& kv) { return kv.second.model == m; });
        if (used) {
            ++it;
        } else {
            w.loaded.erase(m);
            it = w.released.erase(it);
        }
    }
    for (auto it = w.loading.begin(); it != w.loading.end();) {
        if (++it->second >= 3) {
            w.loaded.insert(it->first);
            it = w.loading.erase(it);
        } else {
            ++it;
        }
    }
    // Movers: anything not ridden, moved by its own task or a rider's task.
    // As in the game: a rider's TASK_GO_STRAIGHT_TO_COORD doesn't move the
    // horse; TASK_GO_TO_COORD_ANY_MEANS on the rider, or a task on the horse,
    // does. SET_ENTITY_VELOCITY only moves what no task is moving (in game,
    // velocity pushes made an untasked horse glide but didn't speed up a
    // tasked one).
    for (auto& kv : w.ents) {
        Ent& e = kv.second;
        if (e.id == 1 || e.mount != 0) continue;  // the local player is moved by the test; riders follow mounts
        Ent* rider = ent(e.rider);
        Ent* tasked = (rider != nullptr && rider->hasTask && rider->taskAnyMeans) ? rider : (e.hasTask ? &e : nullptr);
        const bool pushed = e.velOverride;
        e.velOverride = false;
        if (tasked == nullptr) {
            e.vel = pushed ? e.overrideVel : coop::Vec3{};
            e.pos = e.pos + e.vel * dt;
            continue;
        }
        coop::Vec3 to = tasked->taskTarget - e.pos;
        to.z = 0.f;
        const float d = coop::length(to);
        if (d <= std::max(0.05f, tasked->taskRadius)) {
            e.vel = {};
            e.heading = coop::wrapDegrees(tasked->taskFinalHeading);
            tasked->hasTask = false;
            continue;
        }
        const float speed = taskSpeed(e, tasked->taskBlend);
        const coop::Vec3 dir = to * (1.f / d);
        e.vel = dir * speed;
        e.pos = e.pos + dir * std::min(speed * dt, d);
        e.pos.z += (tasked->taskTarget.z - e.pos.z) * std::min(1.f, dt * 5.f);
        e.heading = coop::forwardToHeading(dir);
    }
    for (auto& kv : w.ents) {
        Ent& e = kv.second;
        if (e.mount == 0) continue;
        if (Ent* m = ent(e.mount)) {
            e.pos = m->pos + coop::Vec3{0, 0, 1};
            e.vel = m->vel;
            e.heading = m->heading;
        }
    }
    for (auto& kv : w.ents) ++kv.second.age;
    w.clock = (w.clock + 1) % 86400;
}

void destroy(int id) {
    if (Ent* e = ent(id)) {
        if (e->mount != 0) dismount(*e);
        if (Ent* r = ent(e->rider)) r->mount = 0;
        w.ents.erase(id);
    }
}

void addEntity(int id, const std::string& model) {
    Ent e;
    e.id = id;
    e.model = coop::joaat(model);
    e.visible = true;
    w.ents[id] = e;
}

void reloadWorld() {
    for (auto it = w.ents.begin(); it != w.ents.end();) {
        it = it->first == 1 ? std::next(it) : w.ents.erase(it);
    }
    w.ents[1].mount = 0;
    w.blips.clear();
    w.loaded.clear();
    w.loading.clear();
    w.released.clear();
}

size_t entityCount() { return w.ents.size(); }
size_t blipCount() { return w.blips.size(); }
int clockSecondsOfDay() { return w.clock; }
void setScreenBusy(bool busy) { w.busy = busy; }
const std::vector<std::string>& textDrawn() { return w.text; }
void clearText() { w.text.clear(); }
const std::vector<uint64_t>& unknownNatives() { return w.unknown; }
int pushesOutOfRange() { return w.badPushes; }
int createPedFailures() { return w.createFailures; }
int blipAddCalls() { return w.blipAdds; }

}  // namespace sim

// ---------------------------------------------------- the fake Script Hook API

void nativeInit(UINT64 hash) {
    sim::g_hash = hash;
    sim::g_args.clear();
}

void nativePush64(UINT64 val) { sim::g_args.push_back(val); }

PUINT64 nativeCall() {
    std::memset(sim::g_result, 0, sizeof(sim::g_result));
    sim::dispatch();
    return sim::g_result;
}
