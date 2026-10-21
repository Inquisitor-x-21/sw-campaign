// Runs the plugin's main loop (plugin/script.cpp: hotkeys, session events,
// stand-ins, regroup, clock sync, overlay, config reload, logs) against the
// simulated world, with real network sessions on loopback as the other players.
//   plugin_script_tests                 host, pause, restart and client scenarios
//   plugin_script_tests log-fallback    game folder not writable
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "coop/session.h"
#include "fake_game.h"
#include "fake_windows.h"
#include "plugin.h"

namespace fs = std::filesystem;
using namespace coop;

HMODULE g_module = nullptr;  // set by plugin/dllmain.cpp, which isn't built here

namespace {

int g_failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                   \
        }                                                                   \
    } while (0)

constexpr uint16_t kPort = 7795;
constexpr int kF6 = 0x75, kF7 = 0x76, kF8 = 0x77, kF9 = 0x78, kF11 = 0x7A;
const uint32_t kCriminal = joaat("g_m_m_unicriminals_01");
const uint32_t kMorgan = joaat("a_c_horse_morgan_bay");

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Another player, driven by the test between the plugin's frames.
struct Peer {
    std::unique_ptr<Session> s;
    PlayerState state;
    WorldState world;  // sent when hosting
    std::vector<Event> events;

    explicit Peer(const std::string& name) {
        SessionSettings ss;
        ss.modVersion = RDR2COOP_VERSION;
        ss.local.name = name;
        ss.local.model = "g_m_m_unicriminals_01";
        ss.local.outfit = 3;
        s = std::make_unique<Session>(ss);
    }
    void pump() {
        s->setLocalState(state, nowMs());
        if (s->role() == Role::Host) s->setWorldState(world);
        s->update(nowMs());
        Event e;
        while (s->pollEvent(e)) events.push_back(e);
    }
    const Event* find(EventType t) const {
        for (const Event& e : events) {
            if (e.type == t) return &e;
        }
        return nullptr;
    }
};

std::vector<Peer*> g_peers;
fs::path g_dir;

// One game frame: the plugin's tick, then the other players, then the world.
// Afterwards sim::textDrawn() holds what that frame put on screen.
void frames(int n) {
    for (int i = 0; i < n; ++i) {
        sim::clearText();
        sim::runScriptFrame();
        for (Peer* p : g_peers) p->pump();
        sim::step(1.f / 60.f);
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

bool framesUntil(int maxFrames, const std::function<bool()>& cond) {
    for (int i = 0; i < maxFrames; ++i) {
        if (cond()) return true;
        frames(1);
    }
    return cond();
}

void press(int vk) {
    sim::setKeyDown(vk, true);
    frames(1);
    sim::setKeyDown(vk, false);
    frames(1);
}

bool onScreen(const std::string& text) {
    for (const std::string& line : sim::textDrawn()) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

std::vector<const sim::Ent*> standIns(uint32_t model) {
    std::vector<const sim::Ent*> out;
    for (int id = 100; id < 2000; ++id) {
        const sim::Ent* e = sim::get(id);
        if (e != nullptr && e->mission && !e->isHorse && e->model == model) out.push_back(e);
    }
    return out;
}

bool oneStandInNear(Vec3 where, float within) {
    const std::vector<const sim::Ent*> v = standIns(kCriminal);
    return v.size() == 1 && distance(v[0]->pos, where) < within;
}

Vec3 localPos() { return sim::get(sim::localPlayer())->pos; }

std::string readLogs(const fs::path& logs) {
    std::string all;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(logs, ec)) {
        std::ifstream f(e.path());
        std::stringstream ss;
        ss << f.rdbuf();
        all += ss.str();
    }
    return all;
}

bool logHas(const std::string& text) {
    return readLogs(g_dir / "RDR2Coop_logs").find(text) != std::string::npos;
}

void writeIni(const std::string& extra) {
    std::ofstream f(g_dir / "RDR2Coop.ini");
    f << "[Network]\nHostPort = " << kPort << "\nJoinPort = " << kPort
      << "\n[Debug]\nStatsIntervalMs = 1000\nVerboseLog = true\n" << extra;
}

void setupWorld() {
    sim::reset();
    sim::addModel("g_m_m_uniduster_01");
    sim::addModel("g_m_m_unicriminals_01");
    sim::addModel("a_c_horse_morgan_bay", true);
    sim::addModel("a_c_horse_kentuckysaddle_black", true);
    sim::get(sim::localPlayer())->pos = {100, 100, 10};
    sim::setModulePath((g_dir / "RDR2Coop.asi").string());
}

// The plugin hosts, a bot joins, walks, rides, gets regrouped with and leaves;
// then the script restarts with a player in the session; then the host leaves.
void testHosting() {
    std::printf("[ RUN  ] hosting\n");
    frames(1);
    CHECK(onScreen("RDR2 Co-op loaded - F7 to host, F8 to join"));
    CHECK(onScreen("F7 Host   F8 Join 127.0.0.1"));
    CHECK(logHas("RDR2 Co-op " RDR2COOP_VERSION " loaded"));

    press(kF7);
    CHECK(framesUntil(30, [] { return onScreen("Hosting on port 7795"); }));
    CHECK(onScreen("HOST"));

    Peer bot("Bot1");
    bot.state.pos = {103, 100, 10};
    bot.state.heading = 90;
    g_peers = {&bot};
    std::string err;
    CHECK(bot.s->join("127.0.0.1", kPort, err));
    CHECK(framesUntil(300, [&] { return bot.s->active(); }));
    CHECK(framesUntil(300, [&] { return oneStandInNear(bot.state.pos, 1.0f); }));
    CHECK(onScreen("Bot1 joined (2/4)"));
    CHECK(onScreen("Players 2/4"));
    CHECK(onScreen("Bot1   3 m   "));  // name, distance and ping columns
    CHECK(!onScreen("via host"));        // the host has a direct link to every player

    // The bot sees the host where the local player stands, and gets the clock.
    SampleResult r;
    CHECK(bot.s->sampleRemote(kHostId, nowMs(), r) && distance(r.state.pos, localPos()) < 0.5f);
    CHECK(bot.find(EventType::WorldSync) != nullptr);

    // The bot mounts up and rides off a bit.
    bot.state.flags = StateFlag::Mounted;
    bot.state.mountModel = kMorgan;
    bot.state.mountPos = {110, 100, 10};
    bot.state.pos = {110, 100, 11};
    CHECK(framesUntil(300, [&] {
        const std::vector<const sim::Ent*> v = standIns(kCriminal);
        if (v.size() != 1 || v[0]->mount == 0) return false;
        const sim::Ent* horse = sim::get(v[0]->mount);
        return horse != nullptr && horse->model == kMorgan && distance(horse->pos, bot.state.mountPos) < 1.0f;
    }));

    // Host regroups with the first player: lands just behind them.
    press(kF11);
    CHECK(distance(localPos(), bot.state.mountPos) < 4.f);
    CHECK(onScreen("Regrouped with Bot1"));

    // The game stops running scripts (pause menu, map, loading screen) for
    // longer than the network timeout. The bot has to stay connected.
    std::printf("[ RUN  ] game stops running scripts\n");
    const int64_t resumeAt = nowMs() + 9000;
    while (nowMs() < resumeAt) {
        bot.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    CHECK(bot.s->active() && bot.find(EventType::Stopped) == nullptr);
    frames(5);
    CHECK(onScreen("Players 2/4"));
    CHECK(standIns(kCriminal).size() == 1);
    CHECK(logHas("the game didn't run scripts for 9."));
    CHECK(logHas("the connection was kept alive"));

    CHECK(framesUntil(300, [] { return logHas("sync | Bot1 | ping"); }));  // StatsIntervalMs = 1000
    CHECK(logHas("perf |"));

    // F6 picks up ini changes: the panel goes, the message comes.
    writeIni("[Display]\nOverlay = false\n");
    press(kF6);
    CHECK(onScreen("Config reloaded"));
    CHECK(!onScreen("Players 2/4"));
    writeIni("");
    press(kF6);
    CHECK(onScreen("Players 2/4"));

    bot.s->leave();
    g_peers = {};
    CHECK(framesUntil(120, [] { return standIns(kCriminal).empty() && onScreen("Bot1 left"); }));
    CHECK(onScreen("Players 1/4"));

    // Script Hook restarts the script after a game reload. The old stand-in's
    // handle now belongs to a stranger with the same model.
    std::printf("[ RUN  ] script restart\n");
    Peer bot2("Bot2");
    bot2.state.pos = {95, 100, 10};
    g_peers = {&bot2};
    CHECK(bot2.s->join("127.0.0.1", kPort, err));
    CHECK(framesUntil(300, [&] { return oneStandInNear(bot2.state.pos, 1.0f); }));
    const std::vector<const sim::Ent*> before = standIns(kCriminal);
    const int oldHandle = before.empty() ? 0 : before[0]->id;
    sim::reloadWorld();
    sim::addEntity(oldHandle, "g_m_m_unicriminals_01");
    sim::startScript(coopScriptMain);
    CHECK(framesUntil(300, [&] { return oneStandInNear(bot2.state.pos, 1.0f); }));
    const std::vector<const sim::Ent*> after = standIns(kCriminal);
    CHECK(after.size() == 1 && after[0]->id != oldHandle);
    const sim::Ent* stranger = sim::get(oldHandle);
    CHECK(stranger != nullptr && !stranger->mission && !stranger->hasTask);
    CHECK(logHas("script restarted"));
    CHECK(onScreen("Players 2/4"));

    // The host leaves: the bot is told, the stand-in goes.
    press(kF9);
    CHECK(framesUntil(120, [&] {
        const Event* e = bot2.find(EventType::Stopped);
        return e != nullptr && e->text == "The host ended the session";
    }));
    CHECK(standIns(kCriminal).empty());
    CHECK(onScreen("F7 Host"));
    g_peers = {};
}

// The plugin joins a host far away: it is teleported next to them, takes their
// clock, shows them, and is told when they end the session.
void testJoining() {
    std::printf("[ RUN  ] joining\n");
    Peer host("Dutch");
    host.state.pos = {500, 500, 20};
    host.world = {18, 30, 0};
    std::string err;
    CHECK(host.s->host(kPort, err));
    g_peers = {&host};
    frames(5);

    press(kF8);
    CHECK(framesUntil(300, [&] { return distance(localPos(), host.state.pos) < 4.f; }));
    CHECK(onScreen("CLIENT"));
    CHECK(onScreen("Dutch"));
    CHECK(std::abs(sim::clockSecondsOfDay() - (18 * 3600 + 30 * 60)) < 300);
    CHECK(framesUntil(300, [&] { return oneStandInNear(host.state.pos, 1.0f); }));
    CHECK(host.find(EventType::PlayerJoined) != nullptr);

    host.s->leave();
    g_peers = {};
    CHECK(framesUntil(120, [] { return onScreen("The host ended the session"); }));
    CHECK(standIns(kCriminal).empty());
    CHECK(onScreen("F7 Host"));
}

// Under Program Files the game folder is read-only: logs go to %LOCALAPPDATA%.
void testLogFallback() {
    std::printf("[ RUN  ] log folder fallback\n");
    { std::ofstream blocker(g_dir / "RDR2Coop_logs"); }  // a file where the folder should go
    const fs::path appData = g_dir / "appdata";
    setenv("LOCALAPPDATA", appData.c_str(), 1);
    sim::startScript(coopScriptMain);
    frames(1);
    const std::string logs = readLogs(appData / "RDR2Coop" / "logs");
    CHECK(logs.find("RDR2 Co-op " RDR2COOP_VERSION " loaded") != std::string::npos);
    CHECK(logs.find("log folder: " + (appData / "RDR2Coop" / "logs").string()) != std::string::npos);
    CHECK(onScreen("game folder is read-only"));
}

}  // namespace

int main(int argc, char** argv) {
    const bool fallback = argc > 1 && std::string(argv[1]) == "log-fallback";
    g_dir = fs::temp_directory_path() / ("rdr2coop_script_test_" + std::to_string(nowMs()));
    fs::create_directories(g_dir);
    writeIni("");
    setupWorld();

    if (fallback) {
        testLogFallback();
    } else {
        sim::startScript(coopScriptMain);
        testHosting();
        testJoining();
    }
    CHECK(sim::unknownNatives().empty());
    CHECK(sim::pushesOutOfRange() == 0);

    std::error_code ec;
    fs::remove_all(g_dir, ec);
    std::printf("\n%s (%d failed checks)\n", g_failures == 0 ? "ALL PASSED" : "FAILED", g_failures);
    std::fflush(stdout);
    // The script thread never returns (like the real fiber), so skip static
    // destructors instead of tearing the world down under it.
    std::_Exit(g_failures == 0 ? 0 : 1);
}
