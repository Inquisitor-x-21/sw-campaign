// A deliberately small stand-in for the game: entities with positions,
// velocities, headings, mounts and simple "walk to point" tasks. It answers
// the natives the plugin uses so the plugin's logic can be exercised
// end-to-end without RDR2. It does not model the real game's animation or
// physics; it checks our decisions, not Rockstar's behaviour.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "coop/mathutil.h"

namespace sim {

struct Ent {
    int id = 0;
    uint32_t model = 0;
    bool isHorse = false;
    coop::Vec3 pos;
    coop::Vec3 vel;
    float heading = 0.f;
    bool mission = false;
    bool invincible = false;
    bool visible = false;
    bool blocksEvents = false;
    int outfit = -1;
    uint32_t relGroup = 0;
    int mount = 0;  // rider -> horse
    int rider = 0;  // horse -> rider
    bool hasTask = false;
    bool taskAnyMeans = false;  // TASK_GO_TO_COORD_ANY_MEANS rather than TASK_GO_STRAIGHT_TO_COORD
    coop::Vec3 taskTarget;
    float taskBlend = 0.f;
    float taskFinalHeading = 0.f;
    float taskRadius = 0.5f;
    bool velOverride = false;
    coop::Vec3 overrideVel;
};

void reset();
void addModel(const std::string& name, bool isHorse = false);
int localPlayer();
Ent* get(int id);
void step(float dt);
// What the game does to our peds behind our back:
void destroy(int id);                               // mission cleanup deletes an entity
void addEntity(int id, const std::string& model);   // a new entity gets a recycled handle
void reloadWorld();                                 // loading a save: everything but the player goes
size_t entityCount();
size_t blipCount();
int clockSecondsOfDay();
void setScreenBusy(bool busy);

const std::vector<std::string>& textDrawn();
void clearText();
const std::vector<uint64_t>& unknownNatives();
int pushesOutOfRange();
int createPedFailures();
int blipAddCalls();

}  // namespace sim
