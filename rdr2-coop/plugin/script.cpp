// The plugin's main loop: hotkeys, the network session, remote stand-ins,
// clock sync, the overlay and the sync-quality log.
#include "plugin.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "avatar.h"
#include "coop/config.h"
#include "coop/log.h"
#include "coop/session.h"
#include "game.h"
#include "overlay.h"

namespace fs = std::filesystem;
using coop::EventType;
using coop::PlayerId;
using coop::Role;
using coop::Vec3;

namespace {

constexpr size_t kKeepLogs = 20;
constexpr int64_t kRegroupWaitMs = 10000;
constexpr int64_t kKeepAliveAfterMs = 250;   // no tick for this long: service the network from the side
constexpr int64_t kReportPauseMs = 1000;     // log pauses in script ticks longer than this

// Not GetTickCount64: it only advances every ~15.6 ms, which shows up as
// stutter in interpolated movement and jitter in state timestamps.
// steady_clock is QueryPerformanceCounter on Windows.
int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool gameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// Key presses (not holds), ignored while another window has focus so two game
// instances on one PC don't both react.
class KeyEdges {
public:
    bool pressed(int vk, bool focused) {
        if (vk <= 0) return false;
        const bool down = focused && (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool& was = down_[vk];
        const bool edge = down && !was;
        was = down;
        return edge;
    }

private:
    std::map<int, bool> down_;
};

struct State {
    bool initialized = false;
    std::string dir;
    coop::Config cfg;
    AvatarTuning tuning;
    std::unique_ptr<coop::Session> session;
    std::map<PlayerId, std::unique_ptr<RemoteAvatar>> avatars;
    Overlay overlay;
    bool panelVisible = true;
    KeyEdges keys;
    bool endSession = false;
    bool hasLastLocal = false;
    Vec3 lastLocal;
    int64_t regroupDeadline = -1;
    int64_t statsAt = 0;
    uint32_t frames = 0;
    std::vector<std::string> lanIps;
    std::string logDir;
    bool logFallback = false;  // logDir isn't next to the game

    // The script fiber holds `net` while it ticks; keepNetworkAlive takes it to
    // service the session while the game isn't running scripts.
    std::mutex net;
    std::atomic<int64_t> lastTickAt{0};
};

// Deliberately never destroyed. Its destructors delete stand-ins through
// natives and shut ENet down, and at game exit they would run from DllMain
// (loader lock, wrong thread), which crashes or hangs instead of quitting.
State& g = *new State();

std::string moduleDir() {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(g_module, path, MAX_PATH);
    return fs::path(path).parent_path().string();
}

bool openLogIn(const fs::path& logs) {
    std::error_code ec;
    fs::create_directories(logs, ec);

    // Keep the newest few logs; every game launch writes its own.
    std::vector<fs::directory_entry> old;
    for (const auto& e : fs::directory_iterator(logs, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".log") old.push_back(e);
    }
    std::sort(old.begin(), old.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        std::error_code ea, eb;
        return a.last_write_time(ea) > b.last_write_time(eb);
    });
    for (size_t i = kKeepLogs - 1; i < old.size(); ++i) fs::remove(old[i].path(), ec);

    SYSTEMTIME t;
    GetLocalTime(&t);
    char name[96];
    std::snprintf(name, sizeof(name), "%04d-%02d-%02d_%02d-%02d-%02d_pid%lu.log", t.wYear, t.wMonth, t.wDay, t.wHour,
                  t.wMinute, t.wSecond, GetCurrentProcessId());
    if (!coop::logOpenFile((logs / name).string())) return false;
    g.logDir = logs.string();
    return true;
}

// Next to the game if possible. Installs under Program Files aren't writable
// without admin rights, so fall back to %LOCALAPPDATA% rather than log nothing.
void openLog() {
    if (openLogIn(fs::path(g.dir) / "RDR2Coop_logs")) return;
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData != nullptr && openLogIn(fs::path(localAppData) / "RDR2Coop" / "logs")) g.logFallback = true;
}

std::vector<std::string> lanAddresses() {
    std::vector<std::string> out;
    char host[256] = {};
    if (gethostname(host, sizeof(host)) != 0) return out;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0) return out;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        char buf[INET_ADDRSTRLEN] = {};
        const auto* sin = reinterpret_cast<const sockaddr_in*>(p->ai_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)) == nullptr) continue;
        const std::string ip = buf;
        if (ip.rfind("127.", 0) != 0 && std::find(out.begin(), out.end(), ip) == out.end()) out.push_back(ip);
    }
    freeaddrinfo(res);
    return out;
}

coop::InterpSettings interpFromConfig(const coop::Config& c) {
    coop::InterpSettings s;
    s.delayMs = c.interpDelayMs;
    s.maxExtrapolationMs = c.maxExtrapolationMs;
    s.teleportDistance = c.teleportDistance;
    return s;
}

void loadConfig(bool reload) {
    coop::Config fresh;
    std::vector<std::string> warnings;
    const std::string path = (fs::path(g.dir) / "RDR2Coop.ini").string();
    const bool found = coop::loadConfigFile(path, fresh, warnings);
    g.cfg = fresh;
    g.tuning = tuningFromConfig(g.cfg);
    g.panelVisible = g.cfg.overlay;
    coop::logSetVerbose(g.cfg.verboseLog);
    if (g.session) g.session->setInterpSettings(interpFromConfig(g.cfg));

    if (!found) LOG_WARN("no RDR2Coop.ini next to the plugin (%s); using defaults", path.c_str());
    for (const std::string& w : warnings) LOG_WARN("RDR2Coop.ini %s", w.c_str());
    LOG_INFO("config %s: %s", reload ? "reloaded" : "loaded", coop::describeConfig(g.cfg).c_str());

    const int64_t now = nowMs();
    if (!warnings.empty()) {
        g.overlay.message("RDR2Coop.ini: " + std::to_string(warnings.size()) + " problem(s), see the log", now,
                          Overlay::Tone::Warn, 10000);
    } else if (reload) {
        g.overlay.message("Config reloaded", now, Overlay::Tone::Good);
    }
}

coop::SessionSettings sessionSettings() {
    coop::SessionSettings s;
    s.modVersion = RDR2COOP_VERSION;
    s.local.name = g.cfg.name;
    s.local.model = g.cfg.model;
    s.local.outfit = static_cast<int16_t>(g.cfg.outfit);
    s.maxPlayers = g.cfg.maxPlayers;
    s.sendRateHz = g.cfg.sendRateHz;
    s.interp = interpFromConfig(g.cfg);
    return s;
}

void endSessionNow() {
    g.avatars.clear();  // each stand-in despawns itself
    g.session.reset();
    g.endSession = false;
    g.hasLastLocal = false;
    g.regroupDeadline = -1;
}

void startHost(int64_t now) {
    if (g.session) {
        g.overlay.message("Already in a session (" + coop::keyName(g.cfg.keyLeave) + " to leave)", now,
                          Overlay::Tone::Warn);
        return;
    }
    auto session = std::make_unique<coop::Session>(sessionSettings());
    std::string err;
    if (!session->host(g.cfg.hostPort, err)) {
        LOG_ERROR("couldn't host: %s", err.c_str());
        g.overlay.message("Couldn't host: " + err, now, Overlay::Tone::Warn, 10000);
        return;
    }
    g.session = std::move(session);
    g.lanIps = lanAddresses();
    std::string ips;
    for (const std::string& ip : g.lanIps) ips += (ips.empty() ? "" : ", ") + ip;
    LOG_INFO("hosting on UDP %u; this PC's LAN address(es): %s", g.cfg.hostPort, ips.empty() ? "?" : ips.c_str());
}

void startJoin(int64_t now) {
    if (g.session) {
        g.overlay.message("Already in a session (" + coop::keyName(g.cfg.keyLeave) + " to leave)", now,
                          Overlay::Tone::Warn);
        return;
    }
    auto session = std::make_unique<coop::Session>(sessionSettings());
    std::string err;
    if (!session->join(g.cfg.joinAddress, g.cfg.joinPort, err)) {
        LOG_ERROR("couldn't join: %s", err.c_str());
        g.overlay.message("Couldn't join: " + err, now, Overlay::Tone::Warn, 10000);
        return;
    }
    g.session = std::move(session);
    LOG_INFO("joining %s:%u", g.cfg.joinAddress.c_str(), g.cfg.joinPort);
    g.overlay.message("Joining " + g.cfg.joinAddress + ":" + std::to_string(g.cfg.joinPort) + "...", now);
}

// Picks who to regroup with: the host for clients, the first player for the host.
bool regroupTarget(int64_t now, coop::SampleResult& out, std::string& name) {
    if (!g.session || !g.session->active()) return false;
    std::vector<PlayerId> ids = g.session->remoteIds();
    if (g.session->role() == Role::Client) ids = {coop::kHostId};
    for (PlayerId id : ids) {
        if (g.session->sampleRemote(id, now, out)) {
            const coop::PlayerInfo* info = g.session->remoteInfo(id);
            name = info != nullptr ? info->name : "player";
            return true;
        }
    }
    return false;
}

bool regroup(int64_t now) {
    coop::SampleResult r;
    std::string name;
    if (!regroupTarget(now, r, name)) return false;
    const coop::PlayerState& s = r.state;
    const Vec3 pos = s.mounted() ? s.mountPos : s.pos;
    const float heading = s.mounted() ? s.mountHeading : s.heading;
    const Vec3 fwd = coop::headingToForward(heading);
    const Vec3 side{fwd.y, -fwd.x, 0.f};
    const Vec3 spot = pos - fwd * 3.f + side * 1.5f;  // just behind and beside them
    game::teleportLocalPlayer(spot, heading);
    LOG_INFO("regrouped with %s at %.1f %.1f %.1f", name.c_str(), spot.x, spot.y, spot.z);
    g.overlay.message("Regrouped with " + name, now, Overlay::Tone::Good);
    return true;
}

void syncClock(const coop::WorldState& host) {
    if (!g.cfg.clockSync) return;
    const coop::WorldState mine = game::readClock();
    int diff = game::secondsOfDay(host) - game::secondsOfDay(mine);
    if (diff > 43200) diff -= 86400;
    if (diff < -43200) diff += 86400;
    if (std::abs(diff) > 180) {  // 3 game minutes = about 6 real seconds of drift
        game::setClock(host);
        LOG_DEBUG("clock set to host %02d:%02d:%02d (was %+d s off)", host.hour, host.minute, host.second, diff);
    }
}

void handleEvent(const coop::Event& e, int64_t now) {
    switch (e.type) {
        case EventType::Started:
            LOG_INFO("session started: %s (id %d)", e.text.c_str(), g.session->localId());
            if (g.session->role() == Role::Host) {
                std::string msg = "Hosting on port " + std::to_string(g.cfg.hostPort);
                if (!g.lanIps.empty()) msg += " - friends join " + g.lanIps.front();
                g.overlay.message(msg, now, Overlay::Tone::Good, 12000);
            } else {
                g.overlay.message(e.text, now, Overlay::Tone::Good);
                if (g.cfg.teleportOnJoin) g.regroupDeadline = now + kRegroupWaitMs;
            }
            break;
        case EventType::PlayerJoined:
            g.avatars[e.info.id] = std::make_unique<RemoteAvatar>(e.info);
            LOG_INFO("%s joined (id %d, model %s, outfit %d)", e.info.name.c_str(), e.info.id, e.info.model.c_str(),
                     e.info.outfit);
            g.overlay.message(e.info.name + " joined (" + std::to_string(g.session->playerCount()) + "/" +
                                  std::to_string(g.session->maxPlayers()) + ")",
                              now, Overlay::Tone::Good);
            break;
        case EventType::PlayerLeft:
            g.avatars.erase(e.info.id);
            LOG_INFO("%s %s (id %d)", e.info.name.c_str(), e.text.c_str(), e.info.id);
            g.overlay.message(e.info.name + " " + e.text, now);
            break;
        case EventType::WorldSync:
            if (g.session->role() == Role::Client) syncClock(e.world);
            break;
        case EventType::Rejected:
            LOG_WARN("host refused us: %s", e.text.c_str());
            g.overlay.message("Couldn't join: " + e.text, now, Overlay::Tone::Warn, 10000);
            break;
        case EventType::Stopped:
            LOG_INFO("session ended: %s", e.text.c_str());
            g.overlay.message(e.text, now, Overlay::Tone::Warn, 8000);
            g.endSession = true;
            break;
    }
}

void sendLocalState(int64_t now) {
    game::LocalCapture cap = game::captureLocalPlayer();
    if (!cap.ok) return;
    const Vec3 p = cap.state.mounted() ? cap.state.mountPos : cap.state.pos;
    if (g.hasLastLocal && coop::distance(p, g.lastLocal) > 30.f) {
        cap.state.flags |= coop::StateFlag::Teleported;  // fast travel, cutscene, respawn
        LOG_DEBUG("local teleport detected (%.0f m)", coop::distance(p, g.lastLocal));
    }
    g.lastLocal = p;
    g.hasLastLocal = true;
    g.session->setLocalState(cap.state, now);
}

void runSession(int64_t now) {
    const bool busyScreen = game::isLoadingOrFaded();
    if (!busyScreen) sendLocalState(now);
    if (g.session->role() == Role::Host) g.session->setWorldState(game::readClock());

    g.session->update(now);
    coop::Event e;
    while (g.session && g.session->pollEvent(e)) handleEvent(e, now);
    if (g.endSession) {
        endSessionNow();
        return;
    }
    if (busyScreen) return;

    for (auto& kv : g.avatars) {
        coop::SampleResult r;
        if (g.session->sampleRemote(kv.first, now, r)) kv.second->update(r, now, g.tuning);
    }

    if (g.regroupDeadline >= 0) {
        if (regroup(now) || now > g.regroupDeadline) g.regroupDeadline = -1;
    }
}

void handleKeys(int64_t now) {
    const bool focused = gameHasFocus();
    if (g.keys.pressed(g.cfg.keyOverlay, focused)) g.panelVisible = !g.panelVisible;
    if (g.keys.pressed(g.cfg.keyReload, focused)) loadConfig(true);
    if (g.keys.pressed(g.cfg.keyHost, focused)) startHost(now);
    if (g.keys.pressed(g.cfg.keyJoin, focused)) startJoin(now);
    if (g.keys.pressed(g.cfg.keyLeave, focused) && g.session) {
        g.session->leave();
        coop::Event e;
        while (g.session && g.session->pollEvent(e)) handleEvent(e, now);
        endSessionNow();
    }
    if (g.keys.pressed(g.cfg.keyRegroup, focused) && !regroup(now)) {
        g.overlay.message("Nobody to regroup with yet", now, Overlay::Tone::Warn);
    }
}

std::vector<std::string> statusLines(int64_t now) {
    std::vector<std::string> lines;
    std::string title = std::string("RDR2 Co-op ") + RDR2COOP_VERSION;
    if (!g.session) {
        lines.push_back(title);
        lines.push_back(coop::keyName(g.cfg.keyHost) + " Host   " + coop::keyName(g.cfg.keyJoin) + " Join " +
                        g.cfg.joinAddress + "   " + coop::keyName(g.cfg.keyOverlay) + " Hide");
        return lines;
    }
    title += g.session->role() == Role::Host ? "  -  HOST" : "  -  CLIENT";
    lines.push_back(title);
    if (!g.session->active()) {
        lines.push_back("Connecting to " + g.cfg.joinAddress + ":" + std::to_string(g.cfg.joinPort) + "...   " +
                        coop::keyName(g.cfg.keyLeave) + " Cancel");
        return lines;
    }
    lines.push_back("Players " + std::to_string(g.session->playerCount()) + "/" +
                    std::to_string(g.session->maxPlayers()) + "   " + coop::keyName(g.cfg.keyLeave) + " Leave   " +
                    coop::keyName(g.cfg.keyRegroup) + " Regroup");
    for (const coop::RemoteView& v : g.session->remoteViews(now)) {
        char line[160];
        auto it = g.avatars.find(v.info.id);
        std::string where = "-";
        std::string status;
        if (it != g.avatars.end()) {
            if (it->second->spawned() && g.hasLastLocal) {
                where = std::to_string(static_cast<int>(coop::distance(g.lastLocal, it->second->shownPosition()))) + " m";
            }
            if (it->second->status() != "ok") status = "  (" + it->second->status() + ")";
        }
        // Only the host link has a ping; clients reach each other through the host.
        const bool direct = g.session->role() == Role::Host || v.info.id == coop::kHostId;
        const std::string ping = direct ? std::to_string(v.pingMs) + " ms" : "via host";
        std::snprintf(line, sizeof(line), "  %s   %s   %s%s", v.info.name.c_str(), where.c_str(), ping.c_str(),
                      status.c_str());
        lines.push_back(line);
    }
    return lines;
}

void logStats(int64_t now) {
    if (g.cfg.statsIntervalMs == 0) return;
    if (g.statsAt == 0) g.statsAt = now;
    const int64_t span = now - g.statsAt;
    if (span < static_cast<int64_t>(g.cfg.statsIntervalMs)) return;
    const double fps = g.frames * 1000.0 / static_cast<double>(span);
    g.frames = 0;
    g.statsAt = now;
    if (!g.session || !g.session->active()) return;

    const coop::SessionStats st = g.session->stats();
    LOG_INFO("perf | %.0f fps | %d/%d players | states sent %u recv %u relayed %u | bad packets %u | %.1f KB out %.1f KB in",
             fps, g.session->playerCount(), g.session->maxPlayers(), st.statesSent, st.statesReceived, st.statesRelayed,
             st.badPackets, static_cast<double>(st.bytesSent) / 1024.0, static_cast<double>(st.bytesReceived) / 1024.0);
    for (const coop::RemoteView& v : g.session->remoteViews(now)) {
        auto it = g.avatars.find(v.info.id);
        if (it == g.avatars.end()) continue;
        SyncStats& s = it->second->stats();
        if (s.samples == 0) {
            LOG_INFO("sync | %s | no samples yet (%s)", v.info.name.c_str(), it->second->status().c_str());
            continue;
        }
        LOG_INFO("sync | %s | ping %u ms | error avg %.2f m max %.2f m | extrapolated %.0f%% | warps %u | buffer min %d ms",
                 v.info.name.c_str(), v.pingMs, s.errorSum / s.samples, s.errorMax, 100.0 * s.extrapolated / s.samples,
                 s.warps, s.minBufferMs == INT32_MAX ? 0 : s.minBufferMs);
        s.reset();
    }
}

void initOnce() {
    g.dir = moduleDir();
    openLog();
    LOG_INFO("RDR2 Co-op %s loaded from %s (pid %lu)", RDR2COOP_VERSION, g.dir.c_str(), GetCurrentProcessId());
    LOG_INFO("log folder: %s", g.logDir.c_str());
    loadConfig(false);
    g.overlay.message("RDR2 Co-op loaded - " + coop::keyName(g.cfg.keyHost) + " to host, " +
                          coop::keyName(g.cfg.keyJoin) + " to join",
                      nowMs(), Overlay::Tone::Info, 10000);
    if (g.logDir.empty()) {
        g.overlay.message("Couldn't create a log file (game folder read-only?)", nowMs(), Overlay::Tone::Warn, 10000);
    } else if (g.logFallback) {
        g.overlay.message("Logs: " + g.logDir + " (game folder is read-only)", nowMs(), Overlay::Tone::Warn, 10000);
    }
    g.initialized = true;
}

// ENet only answers the others while someone services it, and normally that is
// the script tick. The game may stop running scripts in the pause menu, on the
// map or during loading screens, and the others would then time us out after
// a few seconds. This thread services the session whenever the script hasn't
// ticked for a while. It never calls natives: Session has no game code.
void keepNetworkAlive() {
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const int64_t now = nowMs();
        if (now - g.lastTickAt.load() < kKeepAliveAfterMs) continue;
        std::lock_guard<std::mutex> lock(g.net);
        if (g.session) g.session->update(now);
    }
}

// Script Hook starts the script again when the game reloads (loading a save,
// for example). The session and its sockets live on in g, but the stand-ins
// went with the old world, and their handles may now name other entities.
void onScriptRestart() {
    LOG_INFO("script restarted (game reloaded?); respawning %u stand-in(s)", static_cast<unsigned>(g.avatars.size()));
    for (auto& kv : g.avatars) kv.second->forgetEntities();
    game::forgetCachedHandles();
}

void tick() {
    const int64_t now = nowMs();
    ++g.frames;
    handleKeys(now);
    if (g.session) runSession(now);
    g.overlay.draw(statusLines(now), g.panelVisible, now);
    logStats(now);
}

}  // namespace

void coopScriptMain() {
    {
        std::lock_guard<std::mutex> lock(g.net);
        if (!g.initialized) {
            initOnce();
            g.lastTickAt = nowMs();
            std::thread(keepNetworkAlive).detach();  // runs until the game exits
        } else {
            onScriptRestart();
        }
    }
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(g.net);
            const int64_t pause = nowMs() - g.lastTickAt.load();
            if (pause > kReportPauseMs) {
                LOG_INFO("the game didn't run scripts for %.1f s (pause menu, map or loading screen?)%s",
                         static_cast<double>(pause) / 1000.0, g.session ? "; the connection was kept alive" : "");
            }
            try {
                tick();
            } catch (const std::exception& ex) {
                LOG_ERROR("unexpected error, leaving the session: %s", ex.what());
                if (g.session) g.session->leave();
                endSessionNow();
            }
            g.lastTickAt = nowMs();
        }
        scriptWait(0);  // never while holding `net`: other fibers and the keepalive run now
    }
}
