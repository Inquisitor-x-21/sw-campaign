// Runs the plugin's game-side code (plugin/game.cpp, avatar.cpp, overlay.cpp)
// against the simulated world in tests/sim. A remote player walks, runs,
// stands, mounts and gallops, dismounts, fast-travels and switches to a horse
// breed this "game" doesn't have; the stand-in has to keep up the whole way.
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "avatar.h"
#include "coop/interp.h"
#include "fake_game.h"
#include "game.h"
#include "overlay.h"

using namespace coop;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                   \
        }                                                                   \
    } while (0)

struct PhaseStats {
    const char* name;
    double from, to;
    double errSum = 0;
    double errMax = 0;
    int n = 0;
};

void setupModels() {
    sim::addModel("g_m_m_uniduster_01");
    sim::addModel("g_m_m_unicriminals_01");
    sim::addModel("a_c_horse_morgan_bay", true);
    sim::addModel("a_c_horse_kentuckysaddle_black", true);
}

// What the remote player is doing at time t (seconds).
struct Script {
    MoveState move;
    Vec3 vel;
    bool mounted;
    uint32_t horse;
};

Script remoteAt(double t) {
    const uint32_t morgan = joaat("a_c_horse_morgan_bay");
    if (t < 5) return {MoveState::Walk, {1.5f, 0, 0}, false, 0};
    if (t < 8) return {MoveState::Run, {0, 3.5f, 0}, false, 0};
    if (t < 10) return {MoveState::Idle, {}, false, 0};
    if (t < 20) return {MoveState::Sprint, {10.f, 0, 0}, true, morgan};
    if (t < 22) return {MoveState::Idle, {}, true, morgan};
    if (t < 25) return {MoveState::Walk, {-1.5f, 0, 0}, false, 0};
    if (t < 27) return {MoveState::Idle, {}, false, 0};
    if (t < 30) return {MoveState::Walk, {2.f, 0, 0}, true, 0xDEADBEEFu};  // breed we don't have
    return {MoveState::Idle, {}, false, 0};
}

int runScenario(MovementMode mode) {
    sim::reset();
    setupModels();
    const int failuresBefore = g_failures;

    PlayerInfo info;
    info.id = 2;
    info.name = "Sadie";
    info.model = "g_m_m_unicriminals_01";
    info.outfit = 160;  // more than the model has: clamped
    AvatarTuning tuning;
    tuning.onFoot = mode;
    tuning.mounted = mode;

    std::vector<PhaseStats> phases = {{"walk", 1, 5},      {"run", 5.5, 8},     {"stand", 8.6, 10},
                                      {"gallop", 11, 20},  {"stand mounted", 20.6, 22}, {"walk again", 22.6, 25},
                                      {"after fast travel", 25.6, 27}, {"fallback horse", 27.8, 30}};

    {
        RemoteAvatar avatar(info);
        SnapshotBuffer buffer;
        InterpSettings interp;
        struct Packet {
            int64_t arrive;
            PlayerState s;
        };
        std::deque<Packet> inFlight;

        Vec3 truth{0, 0, 10};
        float heading = 270.f;
        const double dt = 1.0 / 60.0;
        const int64_t localStart = 500000;
        int64_t lastSend = -1000;
        bool spawnedEarly = false, horseMorgan = false, mountedOk = false, dismountedOk = false,
             fallbackHorse = false, caughtUpAfterTeleport = false;
        int warpsBeforeTeleport = 0;

        for (int frame = 0; frame < 33 * 60; ++frame) {
            const double t = frame * dt;
            const int64_t now = localStart + static_cast<int64_t>(t * 1000.0);
            const Script sc = remoteAt(t);
            if (std::fabs(t - 25.0) < dt / 2) truth = truth + Vec3{500.f, 0, 0};  // fast travel
            truth = truth + sc.vel * static_cast<float>(dt);
            if (length2d(sc.vel) > 0.1f) heading = forwardToHeading(sc.vel);

            if (now - lastSend >= 50) {
                lastSend = now;
                PlayerState s;
                s.timeMs = static_cast<uint32_t>(now - localStart + 7777);  // sender clock differs from ours
                s.pos = sc.mounted ? truth + Vec3{0, 0, 1} : truth;
                s.vel = sc.vel;
                s.heading = heading;
                s.move = sc.move;
                if (sc.mounted) {
                    s.flags |= StateFlag::Mounted;
                    s.mountModel = sc.horse;
                    s.mountPos = truth;
                    s.mountVel = sc.vel;
                    s.mountHeading = heading;
                }
                const int jitter = static_cast<int>((frame * 7919) % 21) - 10;  // deterministic +-10 ms
                inFlight.push_back({now + 30 + jitter, s});
            }
            while (!inFlight.empty() && inFlight.front().arrive <= now) {
                buffer.push(inFlight.front().s, now);
                inFlight.pop_front();
            }

            SampleResult r;
            if (buffer.sample(now, interp, r)) avatar.update(r, now, tuning);
            sim::step(static_cast<float>(dt));

            if (!avatar.spawned()) continue;
            if (t < 0.5) spawnedEarly = true;

            // Find our stand-in and, if any, its horse.
            const sim::Ent* rider = nullptr;
            for (int id = 100; id < 200; ++id) {
                const sim::Ent* e = sim::get(id);
                if (e != nullptr && !e->isHorse) rider = e;
            }
            if (rider == nullptr) continue;
            const sim::Ent* horse = rider->mount != 0 ? sim::get(rider->mount) : nullptr;
            const Vec3 shown = horse != nullptr ? horse->pos : rider->pos;
            const Vec3 target = avatar.shownPosition();
            const double err = distance(shown, target);
            for (PhaseStats& p : phases) {
                if (t >= p.from && t < p.to) {
                    p.errSum += err;
                    p.errMax = std::max(p.errMax, err);
                    ++p.n;
                }
            }

            if (t > 0.5 && t < 1.0) {
                CHECK(rider->visible && rider->mission && rider->invincible && rider->blocksEvents);
                CHECK(rider->outfit == 160 % 150);
                CHECK(rider->model == joaat("g_m_m_unicriminals_01"));
                CHECK(rider->relGroup == joaat("RDR2COOP_PLAYERS"));
            }
            if (t > 11.0 && t < 11.1) {
                mountedOk = horse != nullptr;
                horseMorgan = horse != nullptr && horse->model == joaat("a_c_horse_morgan_bay");
            }
            if (t > 23.0 && t < 23.1) dismountedOk = rider->mount == 0;
            if (t > 24.9 && t < 25.0) warpsBeforeTeleport = static_cast<int>(avatar.stats().warps);
            if (t > 25.6 && t < 25.7) caughtUpAfterTeleport = distance(shown, truth) < 3.0;
            if (t > 28.5 && t < 28.6) {
                fallbackHorse = horse != nullptr && horse->model == joaat("a_c_horse_kentuckysaddle_black");
            }
        }

        std::printf("    stand-in error vs. target (%s):\n", toString(mode));
        for (const PhaseStats& p : phases) {
            std::printf("      %-18s avg %.2f m  max %.2f m\n", p.name, p.n ? p.errSum / p.n : -1.0, p.errMax);
        }
        CHECK(spawnedEarly);
        CHECK(mountedOk);
        CHECK(horseMorgan);
        CHECK(dismountedOk);
        CHECK(caughtUpAfterTeleport);
        CHECK(fallbackHorse);
        if (mode == MovementMode::Hybrid) CHECK(warpsBeforeTeleport == 0);  // no warping in normal play

        for (const PhaseStats& p : phases) {
            CHECK(p.n > 0);
            const double avg = p.n ? p.errSum / p.n : 99;
            if (mode == MovementMode::Teleport) {
                CHECK(avg < 0.25);
            } else {
                CHECK(avg < 1.5);   // keeps up on foot and on horseback
                CHECK(p.errMax < 6.0);
            }
        }
        CHECK(sim::blipCount() == 1);
    }  // avatar destroyed here: everything it spawned must go

    CHECK(sim::entityCount() == 1);  // only the local player is left
    CHECK(sim::blipCount() == 0);
    CHECK(sim::unknownNatives().empty());
    CHECK(sim::pushesOutOfRange() == 0);
    CHECK(sim::createPedFailures() == 0);
    return g_failures - failuresBefore;
}

void testLocalCaptureAndTeleport() {
    sim::reset();
    setupModels();
    sim::Ent* me = sim::get(1);
    me->pos = {10, 20, 30};
    me->vel = {3.5f, 0, 0};
    me->heading = 90;
    game::LocalCapture c = game::captureLocalPlayer();
    CHECK(c.ok && !c.state.mounted());
    CHECK(c.state.move == MoveState::Run);
    CHECK(std::fabs(c.state.pos.x - 10) < 1e-4);

    // Put the player on a horse (spawned through the plugin's own helpers).
    game::ModelRequest req;
    const uint32_t morgan = joaat("a_c_horse_morgan_bay");
    int frames = 0;
    while (game::ensureModel(req, morgan, 1000 + frames) != 1 && frames < 10) {
        sim::step(0.016f);
        ++frames;
    }
    CHECK(frames <= 4);
    rdr::Ped horse = game::spawnPed(morgan, {12, 20, 30}, 0);
    CHECK(horse != 0);
    rdr::PED::SET_PED_ONTO_MOUNT(1, horse, -1, TRUE);
    sim::get(horse)->vel = {11, 0, 0};
    c = game::captureLocalPlayer();
    CHECK(c.state.mounted() && c.state.mountModel == morgan && c.state.move == MoveState::Sprint);

    game::teleportLocalPlayer({500, 600, 70}, 45);
    CHECK(std::fabs(sim::get(horse)->pos.x - 500) < 1e-3);  // the horse moved...
    CHECK(sim::get(1)->mount == horse);                     // ...with the player still on it

    WorldState w{18, 45, 30};
    game::setClock(w);
    CHECK(game::secondsOfDay(game::readClock()) == 18 * 3600 + 45 * 60 + 30);
    CHECK(sim::unknownNatives().empty() && sim::pushesOutOfRange() == 0);
}

// Our rider stand-in (mission ped with the expected model) and its horse.
struct StandIn {
    const sim::Ent* rider = nullptr;
    const sim::Ent* horse = nullptr;
};

StandIn findStandIn(uint32_t riderModel) {
    StandIn s;
    for (int id = 100; id < 1000; ++id) {
        const sim::Ent* e = sim::get(id);
        if (e != nullptr && e->mission && !e->isHorse && e->model == riderModel) s.rider = e;
    }
    if (s.rider != nullptr && s.rider->mount != 0) s.horse = sim::get(s.rider->mount);
    return s;
}

// The game takes stand-ins away in three ways: mission cleanup deletes them,
// a deleted ped's handle is recycled for a new entity, and loading a save wipes
// the world and restarts the script. Each time the stand-in must come back as
// the same model on the same breed, and entities that aren't ours must not be
// moved, tasked or deleted.
void testRespawns() {
    sim::reset();
    setupModels();
    sim::addModel("a_m_m_rancher_01");
    const uint32_t riderModel = joaat("g_m_m_unicriminals_01");
    const uint32_t morgan = joaat("a_c_horse_morgan_bay");

    PlayerInfo info;
    info.id = 3;
    info.name = "Javier";
    info.model = "g_m_m_unicriminals_01";
    info.outfit = 0;
    AvatarTuning tuning;
    int64_t now = 100000;
    {
        RemoteAvatar avatar(info);
        auto run = [&](double seconds) {
            for (int i = 0; i < static_cast<int>(seconds * 60); ++i) {
                SampleResult r;
                r.state.flags = StateFlag::Mounted;
                r.state.mountModel = morgan;
                r.state.pos = {50, 50, 11};
                r.state.mountPos = {50, 50, 10};
                avatar.update(r, now, tuning);
                sim::step(1.f / 60.f);
                now += 16;
            }
        };
        auto mountedOnMorgan = [&]() {
            const StandIn s = findStandIn(riderModel);
            return s.rider != nullptr && s.horse != nullptr && s.horse->model == morgan;
        };

        // Longer than the 8 s model request timeout, so a stale request would show.
        run(10);
        CHECK(mountedOnMorgan());

        // Mission cleanup deletes rider and horse, and their models are evicted
        // before we look again (a loading screen, when stand-ins aren't updated).
        StandIn s = findStandIn(riderModel);
        CHECK(s.rider != nullptr && s.horse != nullptr);
        if (s.rider != nullptr && s.horse != nullptr) {
            sim::destroy(s.rider->id);
            sim::destroy(s.horse->id);
        }
        sim::step(1.f / 60.f);
        run(3);
        CHECK(mountedOnMorgan());  // not the fallback model or breed

        // The rider is deleted and its handle goes to a new, unrelated ped.
        s = findStandIn(riderModel);
        const int recycled = s.rider != nullptr ? s.rider->id : 0;
        if (recycled != 0) {
            sim::destroy(recycled);
            sim::addEntity(recycled, "a_m_m_rancher_01");
        }
        run(3);
        CHECK(mountedOnMorgan());
        const sim::Ent* stranger = sim::get(recycled);
        CHECK(stranger != nullptr && !stranger->mission && !stranger->hasTask && stranger->mount == 0);

        // Loading a save: the world goes, the script restarts, and the old
        // handles come back as other peds, one even with our rider's model.
        s = findStandIn(riderModel);
        const int oldRider = s.rider != nullptr ? s.rider->id : 0;
        const int oldHorse = s.horse != nullptr ? s.horse->id : 0;
        sim::reloadWorld();
        if (oldRider != 0) sim::addEntity(oldRider, "g_m_m_unicriminals_01");
        if (oldHorse != 0) sim::addEntity(oldHorse, "a_m_m_rancher_01");
        avatar.forgetEntities();
        game::forgetCachedHandles();
        run(3);
        CHECK(mountedOnMorgan());
        const StandIn after = findStandIn(riderModel);
        CHECK(after.rider != nullptr && after.rider->id != oldRider);
        CHECK(after.rider != nullptr && after.rider->relGroup == joaat("RDR2COOP_PLAYERS"));
        for (int id : {oldRider, oldHorse}) {
            const sim::Ent* e = sim::get(id);
            CHECK(e != nullptr && !e->mission && !e->hasTask && e->mount == 0 && e->rider == 0);
        }
        CHECK(sim::blipCount() == 1);
    }
    // Only the local player and the two peds that got the old handles are left.
    CHECK(sim::entityCount() == 3);
    CHECK(sim::blipCount() == 0);
    CHECK(sim::unknownNatives().empty());
    CHECK(sim::pushesOutOfRange() == 0);  // also counts deleting an entity that isn't ours
}

// A blip style the game rejects is tried once per change, not every frame.
void testBlipStyleRejected() {
    sim::reset();
    setupModels();
    PlayerInfo info;
    info.id = 2;
    info.name = "Hosea";
    info.model = "g_m_m_uniduster_01";
    AvatarTuning tuning;
    tuning.blipStyle = "BLIP_STYLE_MISSING";
    {
        RemoteAvatar avatar(info);
        SampleResult r;
        r.state.pos = {5, 5, 10};
        int64_t now = 1000;
        for (int i = 0; i < 120; ++i, now += 16) {
            avatar.update(r, now, tuning);
            sim::step(1.f / 60.f);
        }
        CHECK(avatar.spawned());
        CHECK(sim::blipAddCalls() == 1);
        CHECK(sim::blipCount() == 0);

        tuning.blipStyle = "BLIP_STYLE_MP_PLAYER";  // fixed in the ini, F6
        for (int i = 0; i < 10; ++i, now += 16) avatar.update(r, now, tuning);
        CHECK(sim::blipAddCalls() == 2);
        CHECK(sim::blipCount() == 1);

        tuning.blips = false;
        for (int i = 0; i < 10; ++i, now += 16) avatar.update(r, now, tuning);
        CHECK(sim::blipCount() == 0);
        CHECK(sim::blipAddCalls() == 2);
    }
    CHECK(sim::entityCount() == 1);
    CHECK(sim::unknownNatives().empty() && sim::pushesOutOfRange() == 0);
}

void testOverlayDraws() {
    sim::reset();
    Overlay o;
    o.message("Arthur joined (2/4)", 1000, Overlay::Tone::Good, 5000);
    sim::clearText();
    o.draw({"RDR2 Co-op 0.1.0  -  HOST", "Players 2/4"}, true, 2000);
    const auto& text = sim::textDrawn();
    CHECK(text.size() == 3);
    CHECK(!text.empty() && text[0] == "RDR2 Co-op 0.1.0  -  HOST");
    CHECK(text.size() == 3 && text[2] == "Arthur joined (2/4)");
    sim::clearText();
    o.draw({"panel"}, false, 7000);  // panel hidden, message expired
    CHECK(sim::textDrawn().empty());

    // A short message behind a long one still disappears on time.
    o.message("Hosting on port 7777", 10000, Overlay::Tone::Good, 12000);
    o.message("Bot1 joined (2/4)", 10000, Overlay::Tone::Good, 6000);
    sim::clearText();
    o.draw({}, false, 18000);
    CHECK(sim::textDrawn().size() == 1);
    CHECK(sim::textDrawn().size() == 1 && sim::textDrawn()[0] == "Hosting on port 7777");
    CHECK(sim::pushesOutOfRange() == 0);
}

}  // namespace

int main() {
    std::printf("[ RUN  ] local capture, teleport, clock\n");
    testLocalCaptureAndTeleport();
    std::printf("[ RUN  ] overlay\n");
    testOverlayDraws();
    std::printf("[ RUN  ] respawn after cleanup, recycled handles, reload\n");
    testRespawns();
    std::printf("[ RUN  ] blip style the game rejects\n");
    testBlipStyleRejected();
    std::printf("[ RUN  ] scenario, Teleport movement\n");
    runScenario(MovementMode::Teleport);
    std::printf("[ RUN  ] scenario, Hybrid movement\n");
    runScenario(MovementMode::Hybrid);
    std::printf("\n%s (%d failed checks)\n", g_failures == 0 ? "ALL PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
