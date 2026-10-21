// GENERATED FILE - do not edit by hand.
// Source: plugin/natives.list + community RDR2 native database
// (https://github.com/alloc8or/rdr3-nativedb-data). Regenerate with tools/gen_natives.py.
#pragma once
#include "native_invoker.h"

namespace rdr {
namespace CAMERA {
    constexpr uint64_t IS_SCREEN_FADED_OUT_HASH = 0xF5472C80DF2FF847ull;
    inline BOOL IS_SCREEN_FADED_OUT() { return invoke<BOOL>(IS_SCREEN_FADED_OUT_HASH); }
} // namespace CAMERA

namespace CLOCK {
    constexpr uint64_t GET_CLOCK_HOURS_HASH = 0xC82CF208C2B19199ull;
    /// Gets the current ingame hour, expressed without zeros. (09:34 will be represented as 9)
    inline int GET_CLOCK_HOURS() { return invoke<int>(GET_CLOCK_HOURS_HASH); }
    constexpr uint64_t GET_CLOCK_MINUTES_HASH = 0x4E162231B823DBBFull;
    /// Gets the current ingame clock minute.
    inline int GET_CLOCK_MINUTES() { return invoke<int>(GET_CLOCK_MINUTES_HASH); }
    constexpr uint64_t GET_CLOCK_SECONDS_HASH = 0xB6101ABE62B5F080ull;
    /// Gets the current ingame clock second. Note that ingame clock seconds change really fast since a day in RDR is only 48 minutes in real life.
    inline int GET_CLOCK_SECONDS() { return invoke<int>(GET_CLOCK_SECONDS_HASH); }
    constexpr uint64_t SET_CLOCK_TIME_HASH = 0x3A52C59FFB2DEED8ull;
    /// SET_CLOCK_TIME(12, 34, 56);
    inline void SET_CLOCK_TIME(int hour, int minute, int second) { return invoke<void>(SET_CLOCK_TIME_HASH, hour, minute, second); }
} // namespace CLOCK

namespace DLC {
    constexpr uint64_t GET_IS_LOADING_SCREEN_ACTIVE_HASH = 0x71D4BF5890659B0Cull;
    inline BOOL GET_IS_LOADING_SCREEN_ACTIVE() { return invoke<BOOL>(GET_IS_LOADING_SCREEN_ACTIVE_HASH); }
} // namespace DLC

namespace ENTITY {
    constexpr uint64_t DOES_ENTITY_EXIST_HASH = 0xD42BD6EB2E0F1677ull;
    /// Checks if the Entity exists
    inline BOOL DOES_ENTITY_EXIST(Entity entity) { return invoke<BOOL>(DOES_ENTITY_EXIST_HASH, entity); }
    constexpr uint64_t GET_ENTITY_COORDS_HASH = 0xA86D5F069399F44Dull;
    /// Gets the current coordinates for a specified entity.
    /// `entity` = The entity to get the coordinates from.
    /// `alive` = Unused by the game, potentially used by debug builds in order to assert whether or not an entity was alive.
    /// If entity is a ped and it's in a vehicle or on a mount the coords of that entity are returned. Set 'realCoords' to true when you need the true ped coords.
    inline NVec3 GET_ENTITY_COORDS(Entity entity, BOOL alive, BOOL realCoords) { return invoke<NVec3>(GET_ENTITY_COORDS_HASH, entity, alive, realCoords); }
    constexpr uint64_t GET_ENTITY_HEADING_HASH = 0xC230DD956E2F5507ull;
    /// Returns the heading of the entity in degrees. Also know as the "Yaw" of an entity.
    inline float GET_ENTITY_HEADING(Entity entity) { return invoke<float>(GET_ENTITY_HEADING_HASH, entity); }
    constexpr uint64_t GET_ENTITY_VELOCITY_HASH = 0x4805D2B1D8CF94A9ull;
    inline NVec3 GET_ENTITY_VELOCITY(Entity entity, int p1) { return invoke<NVec3>(GET_ENTITY_VELOCITY_HASH, entity, p1); }
    constexpr uint64_t GET_ENTITY_MODEL_HASH = 0xDA76A9F39210D365ull;
    /// Returns the model hash from the entity
    inline Hash GET_ENTITY_MODEL(Entity entity) { return invoke<Hash>(GET_ENTITY_MODEL_HASH, entity); }
    constexpr uint64_t SET_ENTITY_COORDS_HASH = 0x06843DA7060A026Bull;
    inline void SET_ENTITY_COORDS(Entity entity, float xPos, float yPos, float zPos, BOOL xAxis, BOOL yAxis, BOOL zAxis, BOOL clearArea) { return invoke<void>(SET_ENTITY_COORDS_HASH, entity, xPos, yPos, zPos, xAxis, yAxis, zAxis, clearArea); }
    constexpr uint64_t SET_ENTITY_COORDS_NO_OFFSET_HASH = 0x239A3351AC1DA385ull;
    /// Axis - Invert Axis Flags
    inline void SET_ENTITY_COORDS_NO_OFFSET(Entity entity, float xPos, float yPos, float zPos, BOOL xAxis, BOOL yAxis, BOOL zAxis) { return invoke<void>(SET_ENTITY_COORDS_NO_OFFSET_HASH, entity, xPos, yPos, zPos, xAxis, yAxis, zAxis); }
    constexpr uint64_t SET_ENTITY_HEADING_HASH = 0xCF2B9C0645C4651Bull;
    inline void SET_ENTITY_HEADING(Entity entity, float heading) { return invoke<void>(SET_ENTITY_HEADING_HASH, entity, heading); }
    constexpr uint64_t SET_ENTITY_VELOCITY_HASH = 0x1C99BB7B6E96D16Full;
    /// Note that the third parameter(denoted as z) is "up and down" with positive numbers encouraging upwards movement.
    inline void SET_ENTITY_VELOCITY(Entity entity, float x, float y, float z) { return invoke<void>(SET_ENTITY_VELOCITY_HASH, entity, x, y, z); }
    constexpr uint64_t SET_ENTITY_AS_MISSION_ENTITY_HASH = 0xDC19C288082E586Eull;
    /// Makes the specified entity (ped, vehicle or object) persistent. Persistent entities will not automatically be removed by the engine.
    inline void SET_ENTITY_AS_MISSION_ENTITY(Entity entity, BOOL scriptHostObject, BOOL grabFromOtherScript) { return invoke<void>(SET_ENTITY_AS_MISSION_ENTITY_HASH, entity, scriptHostObject, grabFromOtherScript); }
    constexpr uint64_t SET_ENTITY_INVINCIBLE_HASH = 0xA5C38736C426FCB8ull;
    /// Sets a ped or an object totally invincible. It doesn't take any kind of damage. Peds will not ragdoll on explosions.
    inline void SET_ENTITY_INVINCIBLE(Entity entity, BOOL toggle) { return invoke<void>(SET_ENTITY_INVINCIBLE_HASH, entity, toggle); }
    constexpr uint64_t IS_ENTITY_DEAD_HASH = 0x7D5B1F88E7504BBAull;
    inline BOOL IS_ENTITY_DEAD(Entity entity) { return invoke<BOOL>(IS_ENTITY_DEAD_HASH, entity); }
    constexpr uint64_t DELETE_ENTITY_HASH = 0x4CD38C78BD19A497ull;
    /// Deletes the specified entity, then sets the handle pointed to by the pointer to NULL.
    inline void DELETE_ENTITY(Entity* entity) { return invoke<void>(DELETE_ENTITY_HASH, entity); }
} // namespace ENTITY

namespace MAP {
    constexpr uint64_t BLIP_ADD_FOR_ENTITY_HASH = 0x23F74C2FDA6E7C61ull;
    inline Blip BLIP_ADD_FOR_ENTITY(Hash blipHash, Entity entity) { return invoke<Blip>(BLIP_ADD_FOR_ENTITY_HASH, blipHash, entity); }
    constexpr uint64_t DOES_BLIP_EXIST_HASH = 0xCD82FA174080B3B1ull;
    inline BOOL DOES_BLIP_EXIST(Blip blip) { return invoke<BOOL>(DOES_BLIP_EXIST_HASH, blip); }
    constexpr uint64_t _SET_BLIP_NAME_HASH = 0x9CB1A1623062F402ull;
    inline void _SET_BLIP_NAME(Blip blip, const char* name) { return invoke<void>(_SET_BLIP_NAME_HASH, blip, name); }
    constexpr uint64_t REMOVE_BLIP_HASH = 0xF2C3C9DA47AAA54Aull;
    inline void REMOVE_BLIP(Blip* blip) { return invoke<void>(REMOVE_BLIP_HASH, blip); }
} // namespace MAP

namespace MISC {
    constexpr uint64_t VAR_STRING_HASH = 0xFA925AC00EB830B9ull;
} // namespace MISC

namespace PED {
    constexpr uint64_t CREATE_PED_HASH = 0xD49F9B0955C367DEull;
    inline Ped CREATE_PED(Hash modelHash, float x, float y, float z, float heading, BOOL isNetwork, BOOL bScriptHostPed, BOOL p7, BOOL p8) { return invoke<Ped>(CREATE_PED_HASH, modelHash, x, y, z, heading, isNetwork, bScriptHostPed, p7, p8); }
    constexpr uint64_t _SET_RANDOM_OUTFIT_VARIATION_HASH = 0x283978A15512B2FEull;
    inline void _SET_RANDOM_OUTFIT_VARIATION(Ped ped, BOOL p1) { return invoke<void>(_SET_RANDOM_OUTFIT_VARIATION_HASH, ped, p1); }
    constexpr uint64_t _EQUIP_META_PED_OUTFIT_PRESET_HASH = 0x77FF8D35EEC6BBC4ull;
    /// Sets the outfit preset for the ped. The presetId is an index which determines its preset outfit. p2 is always false in the scripts.
    /// If p2 is true as player, then certain components like facial hair and hair will not be removed.
    /// Old name: _SET_PED_OUTFIT_PRESET
    inline void _EQUIP_META_PED_OUTFIT_PRESET(Ped ped, int presetId, BOOL p2) { return invoke<void>(_EQUIP_META_PED_OUTFIT_PRESET_HASH, ped, presetId, p2); }
    constexpr uint64_t IS_PED_ON_MOUNT_HASH = 0x460BC76A0E10655Eull;
    inline BOOL IS_PED_ON_MOUNT(Ped ped) { return invoke<BOOL>(IS_PED_ON_MOUNT_HASH, ped); }
    constexpr uint64_t GET_MOUNT_HASH = 0xE7E11B8DCBED1058ull;
    inline Ped GET_MOUNT(Ped ped) { return invoke<Ped>(GET_MOUNT_HASH, ped); }
    constexpr uint64_t SET_PED_ONTO_MOUNT_HASH = 0x028F76B6E78246EBull;
    inline void SET_PED_ONTO_MOUNT(Ped ped, Ped mount, int seatIndex, BOOL p3) { return invoke<void>(SET_PED_ONTO_MOUNT_HASH, ped, mount, seatIndex, p3); }
    constexpr uint64_t SET_BLOCKING_OF_NON_TEMPORARY_EVENTS_HASH = 0x9F8AA94D6D97DBF4ull;
    inline void SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(Ped ped, BOOL toggle) { return invoke<void>(SET_BLOCKING_OF_NON_TEMPORARY_EVENTS_HASH, ped, toggle); }
    constexpr uint64_t SET_PED_KEEP_TASK_HASH = 0x971D38760FBC02EFull;
    inline void SET_PED_KEEP_TASK(Ped ped, BOOL toggle) { return invoke<void>(SET_PED_KEEP_TASK_HASH, ped, toggle); }
    constexpr uint64_t SET_PED_CAN_RAGDOLL_HASH = 0xB128377056A54E2Aull;
    inline void SET_PED_CAN_RAGDOLL(Ped ped, BOOL toggle) { return invoke<void>(SET_PED_CAN_RAGDOLL_HASH, ped, toggle); }
    constexpr uint64_t SET_PED_CAN_BE_TARGETTED_HASH = 0x63F58F7C80513AADull;
    inline void SET_PED_CAN_BE_TARGETTED(Ped ped, BOOL toggle) { return invoke<void>(SET_PED_CAN_BE_TARGETTED_HASH, ped, toggle); }
    constexpr uint64_t SET_PED_FLEE_ATTRIBUTES_HASH = 0x70A2D1137C8ED7C9ull;
    /// https://github.com/femga/rdr3_discoveries/tree/master/AI/FLEE_ATTRIBUTES
    /// attributeFlags:
    /// enum eFleeAttribute
    /// {
    /// FA_FORCE_EXIT_VEHICLE = (1 << 16),
    /// FA_DISABLE_MOUNT_USAGE = (1 << 20),
    inline void SET_PED_FLEE_ATTRIBUTES(Ped ped, int attributeFlags, BOOL enable) { return invoke<void>(SET_PED_FLEE_ATTRIBUTES_HASH, ped, attributeFlags, enable); }
    constexpr uint64_t ADD_RELATIONSHIP_GROUP_HASH = 0xF372BC22FCB88606ull;
    /// The hash of the created relationship group is output in the second parameter.
    inline BOOL ADD_RELATIONSHIP_GROUP(const char* name, Hash* groupHash) { return invoke<BOOL>(ADD_RELATIONSHIP_GROUP_HASH, name, groupHash); }
    constexpr uint64_t SET_RELATIONSHIP_BETWEEN_GROUPS_HASH = 0xBF25EB89375A37ADull;
    inline void SET_RELATIONSHIP_BETWEEN_GROUPS(int relationship, Hash group1, Hash group2) { return invoke<void>(SET_RELATIONSHIP_BETWEEN_GROUPS_HASH, relationship, group1, group2); }
    constexpr uint64_t SET_PED_RELATIONSHIP_GROUP_HASH_HASH = 0xC80A74AC829DDD92ull;
    inline void SET_PED_RELATIONSHIP_GROUP_HASH(Ped ped, Hash relationshipGroup) { return invoke<void>(SET_PED_RELATIONSHIP_GROUP_HASH_HASH, ped, relationshipGroup); }
} // namespace PED

namespace PLAYER {
    constexpr uint64_t PLAYER_PED_ID_HASH = 0x096275889B8E0EE0ull;
    /// Returns current player ped
    inline Ped PLAYER_PED_ID() { return invoke<Ped>(PLAYER_PED_ID_HASH); }
    constexpr uint64_t PLAYER_ID_HASH = 0x217E9DC48139933Dull;
    /// This returns YOUR 'identity' as a Player type.
    /// Always returns 0 in story mode.
    inline Player PLAYER_ID() { return invoke<Player>(PLAYER_ID_HASH); }
} // namespace PLAYER

namespace STREAMING {
    constexpr uint64_t IS_MODEL_IN_CDIMAGE_HASH = 0xD6F3B6D7716CFF8Eull;
    /// Returns whether the specified model exists in the game.
    inline BOOL IS_MODEL_IN_CDIMAGE(Hash model) { return invoke<BOOL>(IS_MODEL_IN_CDIMAGE_HASH, model); }
    constexpr uint64_t IS_MODEL_VALID_HASH = 0x392C8D8E07B70EFCull;
    /// Returns whether the specified model is valid
    inline BOOL IS_MODEL_VALID(Hash model) { return invoke<BOOL>(IS_MODEL_VALID_HASH, model); }
    constexpr uint64_t REQUEST_MODEL_HASH = 0xFA28FE3A6246FC30ull;
    /// Request a model to be loaded into memory.
    inline void REQUEST_MODEL(Hash model, BOOL p1) { return invoke<void>(REQUEST_MODEL_HASH, model, p1); }
    constexpr uint64_t HAS_MODEL_LOADED_HASH = 0x1283B8B89DD5D1B6ull;
    /// Checks if the specified model has loaded into memory.
    inline BOOL HAS_MODEL_LOADED(Hash model) { return invoke<BOOL>(HAS_MODEL_LOADED_HASH, model); }
    constexpr uint64_t SET_MODEL_AS_NO_LONGER_NEEDED_HASH = 0x4AD96EF928BD4F9Aull;
    /// Marks the model as no longer needed.
    inline void SET_MODEL_AS_NO_LONGER_NEEDED(Hash model) { return invoke<void>(SET_MODEL_AS_NO_LONGER_NEEDED_HASH, model); }
    constexpr uint64_t REQUEST_COLLISION_AT_COORD_HASH = 0x0A3720F162A033C9ull;
    inline void REQUEST_COLLISION_AT_COORD(float x, float y, float z) { return invoke<void>(REQUEST_COLLISION_AT_COORD_HASH, x, y, z); }
} // namespace STREAMING

namespace TASK {
    constexpr uint64_t TASK_GO_STRAIGHT_TO_COORD_HASH = 0xD76B57B44F1E6F8Bull;
    /// Go to coords wihtout using navmesh, if timeBeforeTeleport is -1 then it never teleports p8 is 1 or 0 still unknown.
    inline void TASK_GO_STRAIGHT_TO_COORD(Ped ped, float x, float y, float z, float moveBlendSpeedY, int timeBeforeTeleport, float finalHeading, float targetRadius, int p8) { return invoke<void>(TASK_GO_STRAIGHT_TO_COORD_HASH, ped, x, y, z, moveBlendSpeedY, timeBeforeTeleport, finalHeading, targetRadius, p8); }
    constexpr uint64_t TASK_STAND_STILL_HASH = 0x919BE13EED931959ull;
    /// Makes the specified ped stand still for (time) milliseconds.
    inline void TASK_STAND_STILL(Ped ped, int time) { return invoke<void>(TASK_STAND_STILL_HASH, ped, time); }
    constexpr uint64_t CLEAR_PED_TASKS_IMMEDIATELY_HASH = 0xAAA34F8A7CB32098ull;
    /// Immediately stops the pedestrian from whatever it's doing. They stop fighting, animations, etc. they forget what they were doing.
    /// resetCrouch TRUE = ped will stand up if crouching, FALSE = ped will remain crouching if crouched
    inline void CLEAR_PED_TASKS_IMMEDIATELY(Ped ped, BOOL p1, BOOL resetCrouch) { return invoke<void>(CLEAR_PED_TASKS_IMMEDIATELY_HASH, ped, p1, resetCrouch); }
} // namespace TASK

namespace UIDEBUG {
    constexpr uint64_t _BG_SET_TEXT_SCALE_HASH = 0xA1253A3C870B6843ull;
    inline void _BG_SET_TEXT_SCALE(float scaleX, float scaleY) { return invoke<void>(_BG_SET_TEXT_SCALE_HASH, scaleX, scaleY); }
    constexpr uint64_t _BG_SET_TEXT_COLOR_HASH = 0x16FA5CE47F184F1Eull;
    /// https://github.com/femga/rdr3_discoveries/tree/master/useful_info_from_rpfs/colours
    inline void _BG_SET_TEXT_COLOR(int red, int green, int blue, int alpha) { return invoke<void>(_BG_SET_TEXT_COLOR_HASH, red, green, blue, alpha); }
    constexpr uint64_t _BG_DISPLAY_TEXT_HASH = 0x16794E044C9EFB58ull;
    /// Note: you must use VAR_STRING
    inline void _BG_DISPLAY_TEXT(const char* text, float x, float y) { return invoke<void>(_BG_DISPLAY_TEXT_HASH, text, x, y); }
} // namespace UIDEBUG

} // namespace rdr
