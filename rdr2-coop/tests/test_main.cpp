// Unit and loopback tests for the portable core. No game needed:
//   core_tests            run everything
//   core_tests session    run tests whose name contains "session"
#include <enet/enet.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "coop/config.h"
#include "coop/interp.h"
#include "coop/log.h"
#include "coop/protocol.h"
#include "coop/serialize.h"
#include "coop/session.h"

using namespace coop;

namespace {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};
std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Registrar {
    Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};
int g_failures = 0;

#define TEST(name)                                       \
    static void name();                                  \
    static Registrar reg_##name(#name, name);            \
    static void name()

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++g_failures;                                                            \
        }                                                                            \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                    \
    do {                                                                                         \
        const double va_ = (a), vb_ = (b);                                                       \
        if (std::fabs(va_ - vb_) > (eps)) {                                                      \
            std::printf("    FAIL %s:%d: %s = %f, expected %f (+-%f)\n", __FILE__, __LINE__, #a, \
                        va_, vb_, static_cast<double>(eps));                                    \
            ++g_failures;                                                                        \
        }                                                                                        \
    } while (0)

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

PlayerState makeState(float x, float y, uint32_t t) {
    PlayerState s;
    s.timeMs = t;
    s.pos = {x, y, 10.f};
    s.vel = {1.f, 0.f, 0.f};
    s.heading = 270.f;
    s.move = MoveState::Walk;
    return s;
}

}  // namespace

// ------------------------------------------------------------ serialization

TEST(protocol_roundtrip_all_messages) {
    HelloMsg hello;
    hello.modVersion = "0.1.0";
    hello.info.name = "Arthur";
    hello.info.model = "g_m_m_uniduster_01";
    hello.info.outfit = 12;
    HelloMsg h2;
    auto b = encode(hello);
    CHECK(decode(b.data(), b.size(), h2));
    CHECK(h2.protocol == kProtocolVersion && h2.modVersion == "0.1.0");
    CHECK(h2.info.name == "Arthur" && h2.info.model == "g_m_m_uniduster_01" && h2.info.outfit == 12);

    WelcomeMsg w;
    w.yourId = 3;
    w.maxPlayers = 4;
    w.world = {21, 30, 5};
    w.players.push_back({1, "Host", "g_m_m_unicriminals_01", -1});
    w.players.push_back({2, "John", "g_m_m_uniduster_02", 4});
    WelcomeMsg w2;
    b = encode(w);
    CHECK(decode(b.data(), b.size(), w2));
    CHECK(w2.yourId == 3 && w2.maxPlayers == 4 && w2.world.hour == 21 && w2.world.minute == 30);
    CHECK(w2.players.size() == 2 && w2.players[1].name == "John" && w2.players[0].outfit == -1);

    PlayerStateMsg st;
    st.id = 2;
    st.state = makeState(-281.5f, 793.25f, 123456);
    st.state.flags = StateFlag::Mounted;
    st.state.mountModel = joaat("a_c_horse_morgan_bay");
    st.state.mountPos = {-281.f, 793.f, 120.f};
    st.state.mountVel = {5.f, 1.f, 0.f};
    st.state.mountHeading = 90.f;
    PlayerStateMsg st2;
    b = encode(st);
    CHECK(decode(b.data(), b.size(), st2));
    CHECK(st2.id == 2 && st2.state.timeMs == 123456 && st2.state.mounted());
    CHECK_NEAR(st2.state.pos.x, -281.5, 1e-4);
    CHECK(st2.state.mountModel == joaat("a_c_horse_morgan_bay"));
    CHECK_NEAR(st2.state.mountVel.x, 5.0, 1e-6);

    // Not mounted: mount fields are not on the wire, so the message is shorter.
    PlayerStateMsg foot = st;
    foot.state.flags = 0;
    CHECK(encode(foot).size() + 4 + 12 + 12 + 4 == encode(st).size());

    PlayerLeftMsg left{4, LeaveReason::TimedOut};
    PlayerLeftMsg left2;
    b = encode(left);
    CHECK(decode(b.data(), b.size(), left2) && left2.id == 4 && left2.reason == LeaveReason::TimedOut);

    RejectMsg rej{RejectReason::SessionFull, "4 players max"};
    RejectMsg rej2;
    b = encode(rej);
    CHECK(decode(b.data(), b.size(), rej2) && rej2.reason == RejectReason::SessionFull && rej2.detail == "4 players max");
}

TEST(protocol_rejects_malformed) {
    PlayerStateMsg st;
    st.id = 2;
    st.state = makeState(1, 2, 3);
    auto b = encode(st);
    PlayerStateMsg out;
    for (size_t n = 0; n < b.size(); ++n) CHECK(!decode(b.data(), n, out));  // every truncation fails
    auto extra = b;
    extra.push_back(0);
    CHECK(!decode(extra.data(), extra.size(), out));  // trailing junk fails

    PlayerStateMsg nanState = st;
    nanState.state.pos.x = std::nanf("");
    b = encode(nanState);
    CHECK(!decode(b.data(), b.size(), out));  // non-finite positions are refused

    auto wrongType = encode(st);
    HelloMsg hello;
    CHECK(!decode(wrongType.data(), wrongType.size(), hello));

    PlayerStateMsg badMove = st;
    auto bm = encode(badMove);
    bm[1 + 1 + 4 + 2 + 12 + 12 + 4] = 9;  // move state byte out of range
    CHECK(!decode(bm.data(), bm.size(), out));
}

TEST(protocol_sanitizes_text) {
    CHECK(sanitizeName("  Dutch\x01\x7f van der Linde   ") == "Dutch van der Linde");
    CHECK(sanitizeName(std::string(60, 'a')).size() == kMaxNameLen);
    CHECK(sanitizeModel("g_m_m_uniduster_01; DROP") == "g_m_m_uniduster_01DROP");
}

TEST(joaat_matches_game_model_hashes) {
    // Model hashes from the community ped list (femga/rdr3_discoveries).
    CHECK(joaat("mp_male") == 0xF5C1611Eu);
    CHECK(joaat("a_c_horse_kentuckysaddle_black") == 0xF1430568u);
    CHECK(joaat("g_m_m_uniduster_01") == 0x14B7F44Du);
    CHECK(joaat("A_C_HORSE_MORGAN_BAY") == 0x790B9F4Bu);  // case-insensitive
}

// -------------------------------------------------------------------- math

TEST(angles_take_the_short_way) {
    CHECK_NEAR(angleDelta(350.f, 10.f), 20.0, 1e-4);
    CHECK_NEAR(angleDelta(10.f, 350.f), -20.0, 1e-4);
    CHECK_NEAR(lerpAngle(350.f, 10.f, 0.5f), 0.0, 1e-4);
    CHECK_NEAR(wrapDegrees(-90.f), 270.0, 1e-4);
    CHECK_NEAR(forwardToHeading(headingToForward(123.f)), 123.0, 1e-3);
    Vec3 north = headingToForward(0.f);
    Vec3 west = headingToForward(90.f);
    CHECK_NEAR(north.y, 1.0, 1e-5);
    CHECK_NEAR(west.x, -1.0, 1e-5);
}

// ----------------------------------------------------------- interpolation

TEST(interp_tracks_linear_motion_under_jitter) {
    // Sender moves at 4 m/s along X, sending every 50 ms. Packets arrive with
    // 30-90 ms of random delay. The rendered position should sit ~100 ms behind
    // the truth, with only tiny error once the clock estimate settles.
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> delay(30, 90);
    SnapshotBuffer buf;
    InterpSettings cfg;
    cfg.delayMs = 100;
    const uint32_t remoteStart = 50000;
    const int64_t localStart = 1000000;
    struct InFlight {
        int64_t arrive;
        PlayerState s;
    };
    std::vector<InFlight> flight;
    for (int i = 0; i < 200; ++i) {
        const uint32_t t = remoteStart + static_cast<uint32_t>(i * 50);
        PlayerState s = makeState(4.f * (i * 0.05f), 0.f, t);
        s.vel = {4.f, 0.f, 0.f};
        flight.push_back({localStart + i * 50 + delay(rng), s});
    }
    double maxErr = 0;
    int samples = 0;
    int extrapolated = 0;
    for (int64_t now = localStart; now < localStart + 200 * 50; now += 16) {
        for (auto& f : flight) {
            if (f.arrive <= now && f.arrive > now - 16) buf.push(f.s, now);
        }
        SampleResult r;
        if (!buf.sample(now, cfg, r)) continue;
        if (now < localStart + 1500) continue;  // let the clock estimate settle
        // Remote time being shown ~= now - localStart - minDelay(30) - 100 ms.
        const double shownRemoteMs = static_cast<double>(now - localStart) - 30.0 - 100.0;
        const double expectX = 4.0 * shownRemoteMs / 1000.0;
        maxErr = std::max(maxErr, std::fabs(r.state.pos.x - expectX));
        extrapolated += r.extrapolated ? 1 : 0;
        ++samples;
    }
    CHECK(samples > 400);
    CHECK(maxErr < 0.12);              // a few cm: only clock-estimate slack
    CHECK(extrapolated * 10 < samples); // the 100 ms buffer covers almost all jitter
}

TEST(interp_extrapolates_briefly_then_holds) {
    SnapshotBuffer buf;
    InterpSettings cfg;
    cfg.delayMs = 100;
    cfg.maxExtrapolationMs = 250;
    PlayerState a = makeState(0, 0, 1000);
    a.vel = {10.f, 0, 0};
    PlayerState b = makeState(1, 0, 1100);
    b.vel = {10.f, 0, 0};
    buf.push(a, 5000);
    buf.push(b, 5100);
    SampleResult r;
    CHECK(buf.sample(5200, cfg, r));  // render time == b's time
    CHECK_NEAR(r.state.pos.x, 1.0, 1e-3);
    CHECK(buf.sample(5300, cfg, r));  // 100 ms past newest
    CHECK(r.extrapolated);
    CHECK_NEAR(r.state.pos.x, 2.0, 1e-3);
    CHECK(buf.sample(9000, cfg, r));  // long silence: capped at 250 ms
    CHECK_NEAR(r.state.pos.x, 3.5, 1e-3);
    CHECK(r.bufferAheadMs < 0);
}

TEST(interp_holds_across_teleports_instead_of_sliding) {
    SnapshotBuffer buf;
    InterpSettings cfg;
    cfg.delayMs = 0;
    cfg.teleportDistance = 25.f;
    PlayerState a = makeState(0, 0, 1000);
    a.vel = {};
    PlayerState b = makeState(500, 0, 1050);  // fast travel
    b.vel = {};
    buf.push(a, 2000);
    buf.push(b, 2050);
    SampleResult r;
    CHECK(buf.sample(2025, cfg, r));
    CHECK(r.heldAtJump);
    CHECK_NEAR(r.state.pos.x, 0.0, 1e-4);  // no point halfway across the map
    CHECK(buf.sample(2050, cfg, r));
    CHECK_NEAR(r.state.pos.x, 500.0, 1e-4);
}

TEST(interp_survives_clock_wraparound) {
    SnapshotBuffer buf;
    InterpSettings cfg;
    cfg.delayMs = 50;
    const uint32_t start = 0xFFFFFF00u;  // sender clock about to wrap
    for (int i = 0; i < 20; ++i) {
        PlayerState s = makeState(static_cast<float>(i), 0, start + static_cast<uint32_t>(i * 50));
        s.vel = {20.f, 0, 0};
        buf.push(s, 10000 + i * 50);
    }
    SampleResult r;
    CHECK(buf.sample(10000 + 19 * 50, cfg, r));
    CHECK_NEAR(r.state.pos.x, 18.0, 1e-3);
    CHECK(!r.extrapolated);
}

TEST(interp_ignores_duplicates_and_sorts_late_packets) {
    SnapshotBuffer buf;
    buf.push(makeState(0, 0, 1000), 2000);
    buf.push(makeState(2, 0, 1100), 2100);
    buf.push(makeState(1, 0, 1050), 2101);  // arrives late, after the newer one
    buf.push(makeState(1, 0, 1050), 2102);  // duplicate
    CHECK(buf.size() == 3);
    InterpSettings cfg;
    cfg.delayMs = 0;
    SampleResult r;
    CHECK(buf.sample(2075, cfg, r));  // clock offset 1000 => remote time 1075
    CHECK_NEAR(r.state.pos.x, 1.5, 1e-3);
}

TEST(speed_classification) {
    CHECK(classifySpeed(0.1f, false) == MoveState::Idle);
    CHECK(classifySpeed(1.5f, false) == MoveState::Walk);
    CHECK(classifySpeed(3.5f, false) == MoveState::Run);
    CHECK(classifySpeed(6.5f, false) == MoveState::Sprint);
    CHECK(classifySpeed(12.f, true) == MoveState::Sprint);
    CHECK(classifySpeed(5.f, true) == MoveState::Run);
}

// ------------------------------------------------------------------ config

TEST(config_parses_and_warns) {
    Config c;
    std::vector<std::string> warn;
    const char* text =
        "\xEF\xBB\xBF; comment\n"
        "[Network]\n"
        "HostPort = 8000\n"
        "JoinAddress = 192.168.1.20   ; my friend's PC\n"
        "MaxPlayers = 9\n"
        "[Player]\n"
        "Name = \"Sadie\"\n"
        "Model = g_m_m_unicriminals_01\n"
        "Outfit = -1\n"
        "[Sync]\n"
        "OnFootMovement = teleport\n"
        "TeleportDistance = abc\n"
        "[Tuning]\n"
        "MountedTask = horse\n"
        "[Hotkeys]\n"
        "Host = numpad7\n"
        "Leave = none\n"
        "Join = F99\n"
        "[Debug]\n"
        "Verbos = true\n";
    CHECK(parseConfig(text, c, warn));
    CHECK(c.hostPort == 8000);
    CHECK(c.joinAddress == "192.168.1.20");
    CHECK(c.maxPlayers == 4);  // 9 is out of range, default kept
    CHECK(c.name == "Sadie");
    CHECK(c.model == "g_m_m_unicriminals_01");
    CHECK(c.outfit == -1);
    CHECK(c.onFootMovement == MovementMode::Teleport);
    CHECK_NEAR(c.teleportDistance, 25.0, 1e-6);
    CHECK(c.mountedTask == MountedTask::Horse);
    CHECK(c.keyHost == 0x67);
    CHECK(c.keyLeave == 0);
    CHECK(c.keyJoin == 0x77);  // F99 refused, default F8 kept
    CHECK(warn.size() == 4);   // MaxPlayers, TeleportDistance, Join, Verbos
    for (const auto& w : warn) std::printf("    (expected warning) %s\n", w.c_str());

    Config d;
    warn.clear();
    CHECK(parseConfig("[Tuning]\nMountedTask = Saddle\n", d, warn));
    CHECK(d.mountedTask == MountedTask::AnyMeans);  // unknown value, default kept
    CHECK(warn.size() == 1);
}

TEST(config_key_names_roundtrip) {
    for (const char* k : {"F1", "F7", "F24", "A", "Z", "0", "9", "NUMPAD0", "NUMPAD9", "INSERT", "PAGEDOWN", "NONE"}) {
        CHECK(keyName(parseKeyName(k)) == k);
    }
    CHECK(parseKeyName("F25") == -1);
    CHECK(parseKeyName("ctrl") == -1);
}

TEST(shipped_config_file_has_no_warnings) {
    Config c;
    std::vector<std::string> warn;
    const bool ok = loadConfigFile(RDR2COOP_SOURCE_DIR "/config/RDR2Coop.ini", c, warn);
    CHECK(ok);
    for (const auto& w : warn) std::printf("    warning: %s\n", w.c_str());
    CHECK(warn.empty());
    Config defaults;
    CHECK(c.hostPort == defaults.hostPort && c.model == defaults.model && c.keyJoin == defaults.keyJoin);
}

// ----------------------------------------------------------------- session

namespace {

uint16_t g_nextPort = static_cast<uint16_t>(40000 + (nowMs() % 2000) * 7 % 20000);

SessionSettings settingsFor(const std::string& name) {
    SessionSettings s;
    s.local.name = name;
    s.local.model = "g_m_m_uniduster_01";
    s.sendRateHz = 30;
    s.connectTimeoutMs = 2000;
    s.peerTimeoutMinMs = 500;
    s.peerTimeoutMaxMs = 1500;
    s.worldSyncIntervalMs = 200;
    return s;
}

struct Node {
    std::unique_ptr<Session> s;
    std::vector<Event> events;
    float phase = 0.f;
    Node() = default;
    explicit Node(std::unique_ptr<Session> session) : s(std::move(session)) {}
};

// Runs every node until cond() holds or timeoutMs passes.
bool pump(std::vector<Node*> nodes, int timeoutMs, const std::function<bool()>& cond) {
    const int64_t end = nowMs() + timeoutMs;
    while (nowMs() < end) {
        const int64_t t = nowMs();
        for (Node* n : nodes) {
            if (!n->s) continue;
            PlayerState st;
            st.pos = {100.f + 5.f * std::cos(n->phase), 200.f + 5.f * std::sin(n->phase), 50.f};
            st.vel = {1.f, 0.f, 0.f};
            n->phase += 0.01f;
            n->s->setLocalState(st, t);
            n->s->update(t);
            Event e;
            while (n->s->pollEvent(e)) n->events.push_back(e);
        }
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return cond();
}

int countEvents(const Node& n, EventType t) {
    int c = 0;
    for (const auto& e : n.events) c += e.type == t ? 1 : 0;
    return c;
}

}  // namespace

TEST(session_four_players_join_relay_and_leave) {
    const uint16_t port = g_nextPort++;
    Node host{std::make_unique<Session>(settingsFor("Host"))};
    std::string err;
    CHECK(host.s->host(port, err));
    WorldState w{7, 15, 0};
    host.s->setWorldState(w);

    Node c[3];
    for (int i = 0; i < 3; ++i) {
        c[i].s = std::make_unique<Session>(settingsFor("Bot" + std::to_string(i + 1)));
        CHECK(c[i].s->join("127.0.0.1", port, err));
    }
    std::vector<Node*> all = {&host, &c[0], &c[1], &c[2]};
    const bool everyoneIn = pump(all, 4000, [&] {
        for (auto& n : c)
            if (!n.s->active() || n.s->remoteIds().size() != 3) return false;
        return host.s->remoteIds().size() == 3;
    });
    CHECK(everyoneIn);
    CHECK(host.s->playerCount() == 4);

    // Ids are unique and in 2..4; the clients know the session size and clock.
    std::map<PlayerId, int> seen;
    for (auto& n : c) {
        seen[n.s->localId()]++;
        CHECK(n.s->localId() >= 2 && n.s->localId() <= 4);
        CHECK(n.s->maxPlayers() == 4);
        bool gotWorld = false;
        for (const auto& e : n.events) gotWorld |= (e.type == EventType::WorldSync && e.world.hour == 7);
        CHECK(gotWorld);
    }
    CHECK(seen.size() == 3);

    // States flow both directly (host <-> client) and via relay (client <-> client).
    const bool statesFlowing = pump(all, 2000, [&] {
        SampleResult r;
        const int64_t t = nowMs();
        for (auto& n : c) {
            for (PlayerId id : n.s->remoteIds()) {
                if (!n.s->sampleRemote(id, t, r)) return false;
                if (std::fabs(r.state.pos.x - 100.f) > 6.f) return false;
            }
        }
        return true;
    });
    CHECK(statesFlowing);
    CHECK(host.s->stats().statesRelayed > 0);

    // A fifth player is refused with a clear reason.
    Node extra{std::make_unique<Session>(settingsFor("Late"))};
    CHECK(extra.s->join("127.0.0.1", port, err));
    all.push_back(&extra);
    CHECK(pump(all, 3000, [&] { return countEvents(extra, EventType::Stopped) == 1; }));
    CHECK(countEvents(extra, EventType::Rejected) == 1);
    bool saidFull = false;
    for (const auto& e : extra.events) saidFull |= e.text.find("full") != std::string::npos;
    CHECK(saidFull);
    all.pop_back();

    // Drop-out: client 2 leaves; everyone else hears about it.
    const PlayerId leaverId = c[1].s->localId();
    c[1].s->leave();
    c[1].s.reset();
    std::vector<Node*> rest = {&host, &c[0], &c[2]};
    CHECK(pump(rest, 2000, [&] {
        return host.s->remoteInfo(leaverId) == nullptr && c[0].s->remoteInfo(leaverId) == nullptr &&
               c[2].s->remoteInfo(leaverId) == nullptr;
    }));
    bool quitReason = false;
    for (const auto& e : host.events) quitReason |= (e.type == EventType::PlayerLeft && e.text == "left");
    CHECK(quitReason);

    // Drop-in: a new player takes the free slot and gets the same id back.
    Node again{std::make_unique<Session>(settingsFor("Rejoin"))};
    CHECK(again.s->join("127.0.0.1", port, err));
    std::vector<Node*> withNew = {&host, &c[0], &c[2], &again};
    CHECK(pump(withNew, 3000, [&] { return again.s->active() && again.s->remoteIds().size() == 3; }));
    CHECK(again.s->localId() == leaverId);

    // Host ends the session: every client is told.
    host.s->leave();
    std::vector<Node*> clients = {&c[0], &c[2], &again};
    CHECK(pump(clients, 2000, [&] {
        return countEvents(c[0], EventType::Stopped) == 1 && countEvents(c[2], EventType::Stopped) == 1 &&
               countEvents(again, EventType::Stopped) == 1;
    }));
    bool hostEnded = false;
    for (const auto& e : c[0].events) hostEnded |= e.text == "The host ended the session";
    CHECK(hostEnded);
}

TEST(session_detects_crashed_client) {
    const uint16_t port = g_nextPort++;
    Node host{std::make_unique<Session>(settingsFor("Host"))};
    std::string err;
    CHECK(host.s->host(port, err));
    Node c{std::make_unique<Session>(settingsFor("Crashy"))};
    CHECK(c.s->join("127.0.0.1", port, err));
    CHECK(pump({&host, &c}, 3000, [&] { return host.s->remoteIds().size() == 1 && c.s->active(); }));
    // Simulate a crash: stop pumping the client entirely (no goodbye).
    CHECK(pump({&host}, 4000, [&] { return host.s->remoteIds().empty(); }));
    bool timedOut = false;
    for (const auto& e : host.events) timedOut |= (e.type == EventType::PlayerLeft && e.text == "lost connection");
    CHECK(timedOut);
}

TEST(session_refuses_wrong_version_and_busy_port) {
    const uint16_t port = g_nextPort++;
    Node host{std::make_unique<Session>(settingsFor("Host"))};
    std::string err;
    CHECK(host.s->host(port, err));

    Node second{std::make_unique<Session>(settingsFor("Host2"))};
    CHECK(!second.s->host(port, err));  // port already taken
    CHECK(err.find("couldn't open UDP port") != std::string::npos);

    // Raw ENet client speaking a future protocol version.
    ENetHost* raw = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
    CHECK(raw != nullptr);
    ENetAddress addr;
    enet_address_set_host(&addr, "127.0.0.1");
    addr.port = port;
    ENetPeer* peer = enet_host_connect(raw, &addr, kChannelCount, 0);
    bool rejectedForVersion = false;
    bool sentHello = false;
    const int64_t end = nowMs() + 3000;
    while (nowMs() < end && !rejectedForVersion) {
        host.s->update(nowMs());
        Event e;
        while (host.s->pollEvent(e)) host.events.push_back(e);
        ENetEvent ev;
        while (enet_host_service(raw, &ev, 1) > 0) {
            if (ev.type == ENET_EVENT_TYPE_CONNECT && !sentHello) {
                HelloMsg h;
                h.protocol = kProtocolVersion + 7;
                h.modVersion = "9.9.9";
                h.info.name = "Future";
                auto bytes = encode(h);
                enet_peer_send(peer, kChannelControl,
                               enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE));
                sentHello = true;
            } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                RejectMsg r;
                if (decode(ev.packet->data, ev.packet->dataLength, r)) {
                    rejectedForVersion = r.reason == RejectReason::VersionMismatch;
                    std::printf("    (expected) host said: %s\n", r.detail.c_str());
                }
                enet_packet_destroy(ev.packet);
            }
        }
    }
    CHECK(rejectedForVersion);
    CHECK(host.s->remoteIds().empty());
    enet_host_destroy(raw);
}

TEST(session_client_times_out_when_nobody_is_hosting) {
    Node c{std::make_unique<Session>(settingsFor("Lonely"))};
    std::string err;
    CHECK(c.s->join("127.0.0.1", g_nextPort++, err));
    CHECK(pump({&c}, 4000, [&] { return countEvents(c, EventType::Stopped) == 1; }));
    bool clear = false;
    for (const auto& e : c.events) clear |= e.text.find("Couldn't reach the host") != std::string::npos;
    CHECK(clear);
    CHECK(c.s->phase() == Phase::Idle);
}

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const TestCase& t : registry()) {
        if (filter != nullptr && std::strstr(t.name, filter) == nullptr) continue;
        const int before = g_failures;
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf("[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
        ++run;
    }
    std::printf("\n%d tests, %d failed checks\n", run, g_failures);
    return g_failures == 0 ? 0 : 1;
}
