// coopbot - fake co-op players for testing without extra copies of the game.
//
//   coopbot join      Bots join a host (the game, or another coopbot) and ride
//                     or walk circles around the host player.
//   coopbot host      A headless host whose player circles the first player
//                     who joins, for testing the game's client side.
//   coopbot selftest  Host + 3 bots on this PC; checks join, relay, leave,
//                     rejoin and shutdown. Run it first on a new machine.
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "coop/interp.h"
#include "coop/log.h"
#include "coop/protocol.h"
#include "coop/session.h"

using namespace coop;

namespace {

std::atomic<bool> g_stop{false};

void onSignal(int) { g_stop = true; }

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Options {
    std::string mode;
    std::string addr = "127.0.0.1";
    uint16_t port = 7777;
    int bots = 3;
    int maxPlayers = 4;
    std::string pattern = "orbit";
    float radius = 6.f;
    float speed = 2.f;
    bool mounted = false;
    std::string horse = "a_c_horse_morgan_bay";
    std::string model = "g_m_m_uniduster_01";
    int outfit = 0;
    double duration = 0;  // seconds, 0 = until Ctrl+C
    double churn = 0;     // seconds between a random bot leaving and rejoining
    int staggerMs = 1500;
    uint32_t sendRate = 20;
    Vec3 at{-281.f, 793.f, 119.f};  // headless host's fallback spot (Valentine main street)
};

void usage() {
    std::printf(
        "coopbot - fake players for RDR2 Co-op testing\n\n"
        "  coopbot join [options]     bots join a host and move around the host player\n"
        "  coopbot host [options]     headless host that circles the first player to join\n"
        "  coopbot selftest           host + 3 bots on this PC, checks everything works\n\n"
        "options:\n"
        "  --addr IP          host address for join (default 127.0.0.1)\n"
        "  --port N           port (default 7777)\n"
        "  --bots N           number of bots for join, 1-3 (default 3)\n"
        "  --max N            host: max players 2-4 (default 4)\n"
        "  --pattern P        orbit | follow | still (default orbit)\n"
        "  --radius M         orbit radius in metres (default 6)\n"
        "  --speed MPS        movement speed (default 2 on foot; try 9 with --mounted)\n"
        "  --mounted          ride a horse\n"
        "  --horse MODEL      horse model (default a_c_horse_morgan_bay)\n"
        "  --model MODEL      bot ped model (default g_m_m_uniduster_01)\n"
        "  --outfit N         outfit preset (default 0, -1 random)\n"
        "  --duration S       leave after S seconds (default: run until Ctrl+C)\n"
        "  --churn S          every S seconds one bot leaves and rejoins 3 s later\n"
        "  --stagger MS       delay between bots joining (default 1500)\n"
        "  --rate HZ          state updates per second (default 20)\n"
        "  --at X,Y,Z         host: where to stand if nobody in game sends a position\n"
        "                     within 3 s of joining (default -281,793,119 = Valentine)\n");
}

bool parseArgs(int argc, char** argv, Options& o) {
    if (argc < 2) return false;
    o.mode = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--addr") o.addr = next("--addr");
        else if (a == "--port") o.port = static_cast<uint16_t>(std::atoi(next("--port")));
        else if (a == "--bots") o.bots = std::atoi(next("--bots"));
        else if (a == "--max") o.maxPlayers = std::atoi(next("--max"));
        else if (a == "--pattern") o.pattern = next("--pattern");
        else if (a == "--radius") o.radius = static_cast<float>(std::atof(next("--radius")));
        else if (a == "--speed") o.speed = static_cast<float>(std::atof(next("--speed")));
        else if (a == "--mounted") o.mounted = true;
        else if (a == "--horse") o.horse = next("--horse");
        else if (a == "--model") o.model = next("--model");
        else if (a == "--outfit") o.outfit = std::atoi(next("--outfit"));
        else if (a == "--duration") o.duration = std::atof(next("--duration"));
        else if (a == "--churn") o.churn = std::atof(next("--churn"));
        else if (a == "--stagger") o.staggerMs = std::atoi(next("--stagger"));
        else if (a == "--rate") o.sendRate = static_cast<uint32_t>(std::atoi(next("--rate")));
        else if (a == "--at") {
            const char* v = next("--at");
            if (std::sscanf(v, "%f,%f,%f", &o.at.x, &o.at.y, &o.at.z) != 3) {
                std::fprintf(stderr, "--at needs X,Y,Z\n");
                return false;
            }
        }
        else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return false;
        }
    }
    if (o.bots < 1) o.bots = 1;
    if (o.bots > kMaxPlayersLimit - 1) o.bots = kMaxPlayersLimit - 1;
    if (o.radius < 0.5f) o.radius = 0.5f;
    if (o.pattern != "orbit" && o.pattern != "follow" && o.pattern != "still") {
        std::fprintf(stderr, "unknown pattern %s\n", o.pattern.c_str());
        return false;
    }
    return true;
}

SessionSettings botSettings(const Options& o, const std::string& name) {
    SessionSettings s;
    s.modVersion = RDR2COOP_VERSION;
    s.local.name = name;
    s.local.model = o.model;
    s.local.outfit = static_cast<int16_t>(o.outfit);
    s.maxPlayers = o.maxPlayers;
    s.sendRateHz = o.sendRate;
    return s;
}

// Where a bot should be, given the player it moves around.
PlayerState moveAround(const Options& o, int index, int count, const PlayerState& anchor, double t) {
    const Vec3 center = anchor.mounted() ? anchor.mountPos : anchor.pos;
    const Vec3 centerVel = anchor.mounted() ? anchor.mountVel : anchor.vel;
    const float centerHeading = anchor.mounted() ? anchor.mountHeading : anchor.heading;
    const float slot = 6.2831853f * static_cast<float>(index) / static_cast<float>(count);

    Vec3 pos;
    Vec3 vel;
    float heading;
    if (o.pattern == "follow") {
        const Vec3 fwd = headingToForward(centerHeading);
        const Vec3 side{fwd.y, -fwd.x, 0.f};
        const float back = 3.f * static_cast<float>(index / 2 + 1);
        const float lateral = (index % 2 == 0 ? 1.5f : -1.5f);
        pos = center - fwd * back + side * lateral;
        vel = centerVel;
        heading = centerHeading;
    } else if (o.pattern == "still") {
        pos = center + Vec3{o.radius * std::cos(slot), o.radius * std::sin(slot), 0.f};
        vel = {};
        heading = forwardToHeading(center - pos);
    } else {
        const float omega = o.speed / o.radius;
        const float a = slot + omega * static_cast<float>(t);
        pos = center + Vec3{o.radius * std::cos(a), o.radius * std::sin(a), 0.f};
        const Vec3 tangent{-std::sin(a) * o.speed, std::cos(a) * o.speed, 0.f};
        vel = centerVel + tangent;
        heading = length2d(vel) > 0.1f ? forwardToHeading(vel) : forwardToHeading(center - pos);
    }

    PlayerState s;
    s.pos = pos;
    s.vel = vel;
    s.heading = heading;
    s.move = classifySpeed(length2d(vel), o.mounted);
    if (o.mounted) {
        s.flags |= StateFlag::Mounted;
        s.mountModel = joaat(o.horse);
        s.mountPos = pos;
        s.mountVel = vel;
        s.mountHeading = heading;
    }
    return s;
}

struct Bot {
    std::string name;
    std::unique_ptr<Session> session;
    bool wantIn = true;
    int64_t joinAt = 0;
    int64_t lastLog = 0;
};

void drainEvents(Bot& b) {
    Event e;
    while (b.session && b.session->pollEvent(e)) {
        switch (e.type) {
            case EventType::Started:
                LOG_INFO("[%s] %s, id %d, %d/%d players", b.name.c_str(), e.text.c_str(), b.session->localId(),
                         b.session->playerCount(), b.session->maxPlayers());
                break;
            case EventType::PlayerJoined:
                LOG_INFO("[%s] sees %s (id %d) join", b.name.c_str(), e.info.name.c_str(), e.info.id);
                break;
            case EventType::PlayerLeft:
                LOG_INFO("[%s] sees %s (id %d) %s", b.name.c_str(), e.info.name.c_str(), e.info.id, e.text.c_str());
                break;
            case EventType::Rejected:
                LOG_WARN("[%s] rejected: %s", b.name.c_str(), e.text.c_str());
                break;
            case EventType::Stopped:
                LOG_WARN("[%s] stopped: %s", b.name.c_str(), e.text.c_str());
                break;
            case EventType::WorldSync:
                LOG_DEBUG("[%s] world clock %02d:%02d:%02d", b.name.c_str(), e.world.hour, e.world.minute,
                          e.world.second);
                break;
        }
    }
}

int runJoin(const Options& o) {
    std::vector<Bot> bots(static_cast<size_t>(o.bots));
    const int64_t start = nowMs();
    for (int i = 0; i < o.bots; ++i) {
        bots[i].name = "Bot" + std::to_string(i + 1);
        bots[i].joinAt = start + static_cast<int64_t>(i) * o.staggerMs;
    }
    std::mt19937 rng(static_cast<unsigned>(start));
    int64_t nextChurn = o.churn > 0 ? start + static_cast<int64_t>(o.churn * 1000) : 0;
    int64_t lastStatus = 0;
    LOG_INFO("joining %s:%u with %d bot(s), pattern %s, %s", o.addr.c_str(), o.port, o.bots, o.pattern.c_str(),
             o.mounted ? "mounted" : "on foot");

    while (!g_stop) {
        const int64_t now = nowMs();
        if (o.duration > 0 && now - start > static_cast<int64_t>(o.duration * 1000)) break;

        for (int i = 0; i < o.bots; ++i) {
            Bot& b = bots[i];
            if (!b.session && b.wantIn && now >= b.joinAt) {
                b.session = std::make_unique<Session>(botSettings(o, b.name));
                std::string err;
                if (!b.session->join(o.addr, o.port, err)) {
                    LOG_ERROR("[%s] can't join: %s", b.name.c_str(), err.c_str());
                    b.session.reset();
                    b.joinAt = now + 3000;
                    continue;
                }
            }
            if (!b.session) continue;

            SampleResult host;
            if (b.session->active() && b.session->sampleRemote(kHostId, now, host)) {
                const double t = static_cast<double>(now - start) / 1000.0;
                b.session->setLocalState(moveAround(o, i, o.bots, host.state, t), now);
            }
            b.session->update(now);
            drainEvents(b);
            if (b.session->phase() == Phase::Idle) {  // stopped: try again in a few seconds
                b.session.reset();
                b.joinAt = now + 3000;
            }
        }

        if (nextChurn != 0 && now >= nextChurn) {
            nextChurn = now + static_cast<int64_t>(o.churn * 1000);
            std::vector<int> live;
            for (int i = 0; i < o.bots; ++i)
                if (bots[i].session && bots[i].session->active()) live.push_back(i);
            if (!live.empty()) {
                Bot& b = bots[live[rng() % live.size()]];
                LOG_INFO("[%s] churn: leaving, back in 3 s", b.name.c_str());
                b.session->leave();
                drainEvents(b);
                b.session.reset();
                b.joinAt = now + 3000;
            }
        }

        if (now - lastStatus >= 5000) {
            lastStatus = now;
            for (Bot& b : bots) {
                if (!b.session || !b.session->active()) continue;
                std::string seen;
                for (const RemoteView& v : b.session->remoteViews(now)) {
                    seen += " " + v.info.name + "(" + std::to_string(v.info.id) + ")";
                }
                LOG_INFO("[%s] id %d, ping %u ms, %d/%d players, sees:%s", b.name.c_str(), b.session->localId(),
                         b.session->hostPingMs(), b.session->playerCount(), b.session->maxPlayers(), seen.c_str());
            }
        }
        sleepMs(5);
    }

    for (Bot& b : bots) {
        if (b.session) {
            b.session->leave();
            drainEvents(b);
        }
    }
    return 0;
}

int runHost(const Options& o) {
    SessionSettings s = botSettings(o, "HeadlessHost");
    Session host(s);
    std::string err;
    if (!host.host(o.port, err)) {
        LOG_ERROR("can't host: %s", err.c_str());
        return 1;
    }
    LOG_INFO("hosting on UDP %u (max %d players). Join from the game with JoinAddress = this PC.", o.port,
             host.maxPlayers());
    const int64_t start = nowMs();
    bool anchored = false;
    PlayerState anchor;
    int64_t lastStatus = 0;
    int64_t firstJoinAt = -1;
    while (!g_stop) {
        const int64_t now = nowMs();
        if (o.duration > 0 && now - start > static_cast<int64_t>(o.duration * 1000)) break;

        if (!anchored) {
            for (PlayerId id : host.remoteIds()) {
                SampleResult r;
                if (host.sampleRemote(id, now, r)) {
                    anchor = r.state;
                    anchor.vel = {};
                    anchor.mountVel = {};
                    anchored = true;
                    const Vec3 p = anchor.mounted() ? anchor.mountPos : anchor.pos;
                    LOG_INFO("anchored the host player at %.1f %.1f %.1f (first player's position)", p.x, p.y, p.z);
                    break;
                }
            }
            // Only bots joined (they wait for the host's position): use the fallback spot.
            if (!anchored && firstJoinAt >= 0 && now - firstJoinAt > 3000) {
                anchor = PlayerState{};
                anchor.pos = o.at;
                anchored = true;
                LOG_INFO("no player position yet, standing at %.1f %.1f %.1f", o.at.x, o.at.y, o.at.z);
            }
        }
        if (anchored) {
            const double t = static_cast<double>(now - start) / 1000.0;
            host.setLocalState(moveAround(o, 0, 1, anchor, t), now);
        }
        host.setWorldState(WorldState{12, 0, 0});
        host.update(now);
        Event e;
        while (host.pollEvent(e)) {
            if (e.type == EventType::PlayerJoined) {
                LOG_INFO("%s joined (id %d)", e.info.name.c_str(), e.info.id);
                if (firstJoinAt < 0) firstJoinAt = now;
            }
            if (e.type == EventType::PlayerLeft) LOG_INFO("%s %s", e.info.name.c_str(), e.text.c_str());
        }
        if (now - lastStatus >= 5000) {
            lastStatus = now;
            const SessionStats st = host.stats();
            LOG_INFO("%d/%d players, states in %u, out %u, relayed %u", host.playerCount(), host.maxPlayers(),
                     st.statesReceived, st.statesSent, st.statesRelayed);
        }
        sleepMs(5);
    }
    host.leave();
    return 0;
}

// ------------------------------------------------------------------ selftest

struct Node {
    std::string name;
    std::unique_ptr<Session> s;
    std::vector<Event> events;
    Node() = default;
    Node(std::string n, std::unique_ptr<Session> session) : name(std::move(n)), s(std::move(session)) {}
};

bool pumpUntil(std::vector<Node*> nodes, int timeoutMs, const std::function<bool()>& cond) {
    const int64_t end = nowMs() + timeoutMs;
    while (nowMs() < end) {
        const int64_t t = nowMs();
        for (Node* n : nodes) {
            if (!n->s) continue;
            PlayerState st;
            st.pos = {-281.f, 793.f, 119.f};
            n->s->setLocalState(st, t);
            n->s->update(t);
            Event e;
            while (n->s->pollEvent(e)) n->events.push_back(e);
        }
        if (cond()) return true;
        sleepMs(2);
    }
    return cond();
}

int runSelftest(const Options& o) {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
        failures += ok ? 0 : 1;
    };
    const uint16_t port = o.port == 7777 ? 7790 : o.port;
    std::printf("selftest on UDP port %u (loopback only)\n", port);

    Node host{"Host", std::make_unique<Session>(botSettings(o, "Host"))};
    std::string err;
    check(host.s->host(port, err), "host opens its port");
    if (!err.empty()) std::printf("     %s\n", err.c_str());

    std::vector<Node> clients(3);
    for (int i = 0; i < 3; ++i) {
        clients[i].name = "Bot" + std::to_string(i + 1);
        clients[i].s = std::make_unique<Session>(botSettings(o, clients[i].name));
        clients[i].s->join("127.0.0.1", port, err);
    }
    std::vector<Node*> all{&host, &clients[0], &clients[1], &clients[2]};
    check(pumpUntil(all, 5000,
                    [&] {
                        for (auto& c : clients)
                            if (!c.s->active() || c.s->remoteIds().size() != 3) return false;
                        return true;
                    }),
          "3 bots join and everyone sees all 4 players");
    check(pumpUntil(all, 3000,
                    [&] {
                        SampleResult r;
                        for (auto& c : clients)
                            for (PlayerId id : c.s->remoteIds())
                                if (!c.s->sampleRemote(id, nowMs(), r)) return false;
                        return true;
                    }),
          "positions reach every player, including relayed bot-to-bot");

    Node fifth{"Extra", std::make_unique<Session>(botSettings(o, "Extra"))};
    fifth.s->join("127.0.0.1", port, err);
    all.push_back(&fifth);
    check(pumpUntil(all, 4000, [&] { return fifth.s->phase() == Phase::Idle; }) && !fifth.events.empty(),
          "a 5th player is turned away (session full)");
    all.pop_back();

    const PlayerId gone = clients[1].s->localId();
    clients[1].s->leave();
    clients[1].s.reset();
    std::vector<Node*> rest{&host, &clients[0], &clients[2]};
    check(pumpUntil(rest, 3000, [&] { return clients[0].s->remoteInfo(gone) == nullptr; }),
          "a bot drops out and the others are told");

    clients[1].s = std::make_unique<Session>(botSettings(o, "Bot2again"));
    clients[1].s->join("127.0.0.1", port, err);
    check(pumpUntil(all, 4000, [&] { return clients[1].s->active() && clients[1].s->remoteIds().size() == 3; }),
          "a bot drops back in");

    host.s->leave();
    std::vector<Node*> cl{&clients[0], &clients[1], &clients[2]};
    check(pumpUntil(cl, 3000,
                    [&] {
                        for (auto& c : clients)
                            if (c.s->phase() != Phase::Idle) return false;
                        return true;
                    }),
          "host ends the session and every bot is told");

    std::printf(failures == 0 ? "\nselftest PASSED\n" : "\nselftest FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseArgs(argc, argv, o)) {
        usage();
        return 2;
    }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    logToConsole(true);
    if (o.mode == "join") return runJoin(o);
    if (o.mode == "host") return runHost(o);
    if (o.mode == "selftest") return runSelftest(o);
    usage();
    return 2;
}
