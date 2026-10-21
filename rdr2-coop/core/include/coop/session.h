// A co-op session over UDP (ENet). One process hosts; up to three others
// join. Everything runs on the caller's thread: call update() every frame and
// drain pollEvent(). No game code in here, so the same class drives the game
// plugin and the headless test bots.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "coop/interp.h"
#include "coop/protocol.h"

namespace coop {

enum class Role : uint8_t { None, Host, Client };
enum class Phase : uint8_t { Idle, Connecting, Handshaking, Active };

enum class EventType : uint8_t {
    Started,       // host: listening. client: welcomed into the session
    PlayerJoined,  // a remote player appeared (info filled)
    PlayerLeft,    // a remote player left (info filled, text = reason)
    WorldSync,     // client: host's world state arrived
    Rejected,      // client: host refused us (text = reason); Stopped follows
    Stopped,       // session ended for any reason (text = why); always the last event
};

struct Event {
    EventType type = EventType::Started;
    PlayerInfo info;
    WorldState world;
    std::string text;
};

struct SessionSettings {
    std::string modVersion = "0.1.0";
    PlayerInfo local;               // name / model / outfit; id is assigned
    int maxPlayers = kMaxPlayersLimit;  // host only, clamped to 2..4
    uint32_t sendRateHz = 20;
    uint32_t worldSyncIntervalMs = 5000;
    uint32_t connectTimeoutMs = 8000;
    uint32_t peerTimeoutMinMs = 3000;   // how fast a crashed player is dropped
    uint32_t peerTimeoutMaxMs = 8000;
    InterpSettings interp;
};

struct RemoteView {
    PlayerInfo info;
    uint32_t pingMs = 0;            // 0 = unknown (client looking at another client)
    size_t buffered = 0;            // states waiting in the interpolation buffer
    int64_t msSinceLastState = -1;  // -1 = nothing received yet
};

struct SessionStats {
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    uint32_t statesSent = 0;
    uint32_t statesReceived = 0;
    uint32_t statesRelayed = 0;
    uint32_t badPackets = 0;
};

class Session {
public:
    explicit Session(SessionSettings settings);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool host(uint16_t port, std::string& error);
    bool join(const std::string& address, uint16_t port, std::string& error);
    // Tells everyone we're leaving, waits up to ~250 ms for that to go out.
    void leave();

    void update(int64_t nowMs);

    // Call every frame with the local player's state; sent at sendRateHz.
    void setLocalState(const PlayerState& state, int64_t nowMs);
    void setWorldState(const WorldState& world);  // host only

    bool pollEvent(Event& out);

    Role role() const;
    Phase phase() const;
    bool active() const { return phase() == Phase::Active; }
    PlayerId localId() const;
    int playerCount() const;  // everyone in the session including us
    int maxPlayers() const;
    std::vector<PlayerId> remoteIds() const;
    const PlayerInfo* remoteInfo(PlayerId id) const;
    bool sampleRemote(PlayerId id, int64_t nowMs, SampleResult& out);
    std::vector<RemoteView> remoteViews(int64_t nowMs) const;
    uint32_t hostPingMs() const;
    SessionStats stats() const;

    void setInterpSettings(const InterpSettings& s);
    const SessionSettings& settings() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace coop
