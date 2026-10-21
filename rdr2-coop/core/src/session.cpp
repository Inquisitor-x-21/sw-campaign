#include "coop/session.h"

#include <enet/enet.h>

#include <algorithm>
#include <deque>
#include <map>

namespace coop {
namespace {

// Values carried by ENet disconnect notifications.
constexpr enet_uint32 kDisconnectQuit = 0x51;        // a client left on purpose
constexpr enet_uint32 kDisconnectHostClosed = 0x52;  // the host ended the session
constexpr enet_uint32 kDisconnectRejected = 0x53;    // the host refused the client

constexpr int kMaxEventsPerUpdate = 256;

int g_enetRefs = 0;

bool enetAcquire() {
    if (g_enetRefs == 0 && enet_initialize() != 0) return false;
    ++g_enetRefs;
    return true;
}

void enetRelease() {
    if (g_enetRefs > 0 && --g_enetRefs == 0) enet_deinitialize();
}

void sendBytes(ENetPeer* peer, uint8_t channel, const std::vector<uint8_t>& bytes, bool reliable,
               SessionStats& stats) {
    if (peer == nullptr || bytes.empty()) return;
    ENetPacket* packet =
        enet_packet_create(bytes.data(), bytes.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (packet == nullptr) return;
    if (enet_peer_send(peer, channel, packet) != 0) {
        enet_packet_destroy(packet);
        return;
    }
    stats.bytesSent += bytes.size();
}

}  // namespace

struct Session::Impl {
    struct Remote {
        PlayerInfo info;
        SnapshotBuffer buffer;
        int64_t lastStateAt = -1;
        ENetPeer* peer = nullptr;  // host side only
    };

    SessionSettings cfg;
    bool libOk = false;

    Role role = Role::None;
    Phase phase = Phase::Idle;
    ENetHost* host = nullptr;
    ENetPeer* server = nullptr;  // client side: the connection to the host
    PlayerId localId = kNoPlayer;
    int sessionMax = kMaxPlayersLimit;
    std::string joinTarget;

    int64_t startedAt = -1;
    int64_t lastNow = 0;
    int64_t nextSendAt = 0;
    int64_t nextWorldAt = 0;

    std::map<PlayerId, Remote> remotes;
    std::map<ENetPeer*, PlayerId> peerIds;           // host: joined peers
    std::map<ENetPeer*, int64_t> pending;            // host: connected, no Hello yet (value = connect time)
    std::deque<Event> events;

    PlayerState local;
    bool hasLocal = false;
    uint16_t seq = 0;
    int teleportSticky = 0;
    WorldState world;
    SessionStats stats;

    explicit Impl(SessionSettings s) : cfg(std::move(s)) {
        cfg.local.name = sanitizeName(cfg.local.name);
        cfg.local.model = sanitizeModel(cfg.local.model);
        cfg.maxPlayers = std::clamp(cfg.maxPlayers, 2, kMaxPlayersLimit);
        if (cfg.sendRateHz == 0) cfg.sendRateHz = 1;
        libOk = enetAcquire();
    }

    ~Impl() {
        if (host != nullptr) leave();
        if (libOk) enetRelease();
    }

    void emit(EventType type, const PlayerInfo& info = {}, std::string text = {}) {
        Event e;
        e.type = type;
        e.info = info;
        e.text = std::move(text);
        e.world = world;
        events.push_back(std::move(e));
    }

    PlayerInfo localInfo() const {
        PlayerInfo p = cfg.local;
        p.id = localId;
        if (p.name.empty()) p.name = "Player " + std::to_string(localId);
        return p;
    }

    // ---------------------------------------------------------------- start

    bool startHost(uint16_t port, std::string& err) {
        if (phase != Phase::Idle) {
            err = "already in a session";
            return false;
        }
        if (!libOk) {
            err = "network library failed to start";
            return false;
        }
        ENetAddress addr;
        addr.host = ENET_HOST_ANY;
        addr.port = port;
        // One spare slot beyond the player cap so an extra joiner can be told "full".
        host = enet_host_create(&addr, static_cast<size_t>(cfg.maxPlayers - 1 + 1), kChannelCount, 0, 0);
        if (host == nullptr) {
            err = "couldn't open UDP port " + std::to_string(port) + " (is something else using it?)";
            return false;
        }
        role = Role::Host;
        phase = Phase::Active;
        localId = kHostId;
        sessionMax = cfg.maxPlayers;
        startedAt = -1;
        nextSendAt = 0;
        nextWorldAt = 0;
        emit(EventType::Started, localInfo(), "Hosting on port " + std::to_string(port));
        return true;
    }

    bool startClient(const std::string& address, uint16_t port, std::string& err) {
        if (phase != Phase::Idle) {
            err = "already in a session";
            return false;
        }
        if (!libOk) {
            err = "network library failed to start";
            return false;
        }
        ENetAddress addr;
        if (enet_address_set_host(&addr, address.c_str()) != 0) {
            err = "couldn't resolve address '" + address + "'";
            return false;
        }
        addr.port = port;
        host = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
        if (host == nullptr) {
            err = "couldn't create a network socket";
            return false;
        }
        server = enet_host_connect(host, &addr, kChannelCount, 0);
        if (server == nullptr) {
            enet_host_destroy(host);
            host = nullptr;
            err = "couldn't start connecting";
            return false;
        }
        enet_peer_timeout(server, 0, cfg.peerTimeoutMinMs, cfg.peerTimeoutMaxMs);
        role = Role::Client;
        phase = Phase::Connecting;
        joinTarget = address + ":" + std::to_string(port);
        startedAt = -1;
        nextSendAt = 0;
        return true;
    }

    // ----------------------------------------------------------------- stop

    void resetState() {
        role = Role::None;
        phase = Phase::Idle;
        server = nullptr;
        localId = kNoPlayer;
        remotes.clear();
        peerIds.clear();
        pending.clear();
        hasLocal = false;
        teleportSticky = 0;
    }

    void stopLocal(const std::string& reason) {
        if (host != nullptr) {
            enet_host_flush(host);
            enet_host_destroy(host);
            host = nullptr;
        }
        resetState();
        emit(EventType::Stopped, {}, reason);
    }

    void leave() {
        if (host == nullptr) return;
        size_t waiting = 0;
        if (role == Role::Host) {
            for (auto& kv : peerIds) {
                enet_peer_disconnect(kv.first, kDisconnectHostClosed);
                ++waiting;
            }
            for (auto& kv : pending) {
                enet_peer_disconnect(kv.first, kDisconnectHostClosed);
                ++waiting;
            }
        } else if (server != nullptr && phase != Phase::Connecting) {
            enet_peer_disconnect(server, kDisconnectQuit);
            waiting = 1;
        }
        const enet_uint32 start = enet_time_get();
        while (waiting > 0 && enet_time_get() - start < 250) {
            ENetEvent ev;
            if (enet_host_service(host, &ev, 10) <= 0) continue;
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
            if (ev.type == ENET_EVENT_TYPE_DISCONNECT) --waiting;
        }
        stopLocal(role == Role::Host ? "You ended the session" : "You left the session");
    }

    // --------------------------------------------------------------- update

    void update(int64_t now) {
        if (host == nullptr) return;
        lastNow = now;
        if (startedAt < 0) startedAt = now;

        ENetEvent ev;
        int handled = 0;
        while (host != nullptr && handled < kMaxEventsPerUpdate && enet_host_service(host, &ev, 0) > 0) {
            handle(ev, now);
            ++handled;
        }
        if (host == nullptr) return;

        if (role == Role::Client && phase != Phase::Active &&
            now - startedAt > static_cast<int64_t>(cfg.connectTimeoutMs)) {
            stopLocal("Couldn't reach the host at " + joinTarget + " (timed out)");
            return;
        }
        if (role == Role::Host) dropSilentPending(now);
        if (phase == Phase::Active) {
            sendStateIfDue(now);
            if (role == Role::Host) sendWorldIfDue(now);
        }
        enet_host_flush(host);
    }

    void handle(ENetEvent& ev, int64_t now) {
        switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT:
                if (role == Role::Host) {
                    pending[ev.peer] = now;
                    enet_peer_timeout(ev.peer, 0, cfg.peerTimeoutMinMs, cfg.peerTimeoutMaxMs);
                } else if (ev.peer == server) {
                    phase = Phase::Handshaking;
                    HelloMsg hello;
                    hello.modVersion = cfg.modVersion;
                    hello.info = cfg.local;
                    sendBytes(server, kChannelControl, encode(hello), true, stats);
                }
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                stats.bytesReceived += ev.packet->dataLength;
                if (role == Role::Host) {
                    hostReceive(ev.peer, ev.packet->data, ev.packet->dataLength, now);
                } else {
                    clientReceive(ev.packet->data, ev.packet->dataLength, now);
                }
                // Received packets belong to us even if handling ended the session.
                enet_packet_destroy(ev.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                if (role == Role::Host) {
                    hostPeerGone(ev.peer, ev.data);
                } else {
                    std::string why = "Lost connection to the host";
                    if (ev.data == kDisconnectHostClosed) why = "The host ended the session";
                    if (phase == Phase::Connecting) why = "Couldn't reach the host at " + joinTarget;
                    stopLocal(why);
                }
                break;
            default:
                break;
        }
    }

    // ----------------------------------------------------------------- host

    void hostReceive(ENetPeer* peer, const uint8_t* data, size_t size, int64_t now) {
        MsgType type;
        if (!peekType(data, size, type)) {
            ++stats.badPackets;
            return;
        }
        if (type == MsgType::Hello) {
            hostHandleHello(peer, data, size);
        } else if (type == MsgType::PlayerState) {
            hostHandleState(peer, data, size, now);
        } else {
            ++stats.badPackets;
        }
    }

    void reject(ENetPeer* peer, RejectReason reason, const std::string& detail) {
        RejectMsg msg;
        msg.reason = reason;
        msg.detail = detail;
        sendBytes(peer, kChannelControl, encode(msg), true, stats);
        enet_peer_disconnect_later(peer, kDisconnectRejected);
        pending.erase(peer);
    }

    void hostHandleHello(ENetPeer* peer, const uint8_t* data, size_t size) {
        if (peerIds.count(peer) != 0) return;  // already joined
        HelloMsg hello;
        if (!decode(data, size, hello)) {
            ++stats.badPackets;
            reject(peer, RejectReason::BadHello, "");
            return;
        }
        if (hello.protocol != kProtocolVersion) {
            reject(peer, RejectReason::VersionMismatch,
                   "host runs protocol " + std::to_string(kProtocolVersion) + " (mod " + cfg.modVersion +
                       "), you run protocol " + std::to_string(hello.protocol) + " (mod " + hello.modVersion + ")");
            return;
        }
        if (static_cast<int>(peerIds.size()) + 1 >= cfg.maxPlayers) {
            reject(peer, RejectReason::SessionFull, std::to_string(cfg.maxPlayers) + " players max");
            return;
        }
        PlayerId id = kNoPlayer;
        for (int candidate = kHostId + 1; candidate <= cfg.maxPlayers; ++candidate) {
            if (remotes.count(static_cast<PlayerId>(candidate)) == 0) {
                id = static_cast<PlayerId>(candidate);
                break;
            }
        }
        if (id == kNoPlayer) {
            reject(peer, RejectReason::SessionFull, "");
            return;
        }

        pending.erase(peer);
        peerIds[peer] = id;
        PlayerInfo info = hello.info;
        info.id = id;
        if (info.name.empty()) info.name = "Player " + std::to_string(id);

        WelcomeMsg welcome;
        welcome.yourId = id;
        welcome.maxPlayers = static_cast<uint8_t>(cfg.maxPlayers);
        welcome.world = world;
        welcome.players.push_back(localInfo());
        for (const auto& kv : remotes) welcome.players.push_back(kv.second.info);
        sendBytes(peer, kChannelControl, encode(welcome), true, stats);

        PlayerJoinedMsg joined;
        joined.info = info;
        const std::vector<uint8_t> joinedBytes = encode(joined);
        for (const auto& kv : peerIds) {
            if (kv.first != peer) sendBytes(kv.first, kChannelControl, joinedBytes, true, stats);
        }

        Remote& r = remotes[id];
        r.info = info;
        r.peer = peer;
        emit(EventType::PlayerJoined, info);
    }

    void hostHandleState(ENetPeer* peer, const uint8_t* data, size_t size, int64_t now) {
        auto it = peerIds.find(peer);
        if (it == peerIds.end()) return;
        PlayerStateMsg msg;
        if (!decode(data, size, msg)) {
            ++stats.badPackets;
            return;
        }
        msg.id = it->second;  // never trust the id a client claims
        Remote& r = remotes[msg.id];
        r.buffer.push(msg.state, now);
        r.lastStateAt = now;
        ++stats.statesReceived;

        const std::vector<uint8_t> relay = encode(msg);
        for (const auto& kv : peerIds) {
            if (kv.first == peer) continue;
            sendBytes(kv.first, kChannelState, relay, false, stats);
            ++stats.statesRelayed;
        }
    }

    void hostPeerGone(ENetPeer* peer, enet_uint32 data) {
        if (pending.erase(peer) != 0) return;
        auto it = peerIds.find(peer);
        if (it == peerIds.end()) return;  // rejected peer finishing its disconnect
        const PlayerId id = it->second;
        peerIds.erase(it);
        PlayerInfo info;
        auto rit = remotes.find(id);
        if (rit != remotes.end()) {
            info = rit->second.info;
            remotes.erase(rit);
        }
        PlayerLeftMsg left;
        left.id = id;
        left.reason = data == kDisconnectQuit ? LeaveReason::Quit : LeaveReason::TimedOut;
        const std::vector<uint8_t> bytes = encode(left);
        for (const auto& kv : peerIds) sendBytes(kv.first, kChannelControl, bytes, true, stats);
        emit(EventType::PlayerLeft, info, left.reason == LeaveReason::Quit ? "left" : "lost connection");
    }

    void dropSilentPending(int64_t now) {
        for (auto it = pending.begin(); it != pending.end();) {
            if (now - it->second > static_cast<int64_t>(cfg.connectTimeoutMs)) {
                enet_peer_disconnect_now(it->first, kDisconnectRejected);
                it = pending.erase(it);
            } else {
                ++it;
            }
        }
    }

    // --------------------------------------------------------------- client

    void clientReceive(const uint8_t* data, size_t size, int64_t now) {
        MsgType type;
        if (!peekType(data, size, type)) {
            ++stats.badPackets;
            return;
        }
        switch (type) {
            case MsgType::Welcome: {
                WelcomeMsg msg;
                if (phase != Phase::Handshaking || !decode(data, size, msg)) {
                    ++stats.badPackets;
                    return;
                }
                localId = msg.yourId;
                sessionMax = msg.maxPlayers;
                world = msg.world;
                phase = Phase::Active;
                remotes.clear();
                emit(EventType::Started, localInfo(), "Joined " + joinTarget);
                for (const PlayerInfo& p : msg.players) {
                    if (p.id == localId || p.id == kNoPlayer) continue;
                    remotes[p.id].info = p;
                    emit(EventType::PlayerJoined, p);
                }
                emit(EventType::WorldSync);
                break;
            }
            case MsgType::Reject: {
                RejectMsg msg;
                std::string why = "refused";
                if (decode(data, size, msg)) {
                    why = toString(msg.reason);
                    if (!msg.detail.empty()) why += " (" + msg.detail + ")";
                }
                emit(EventType::Rejected, {}, why);
                stopLocal("The host refused the connection: " + why);
                break;
            }
            case MsgType::PlayerJoined: {
                PlayerJoinedMsg msg;
                if (!decode(data, size, msg)) {
                    ++stats.badPackets;
                    return;
                }
                if (msg.info.id == localId || remotes.count(msg.info.id) != 0) return;
                remotes[msg.info.id].info = msg.info;
                emit(EventType::PlayerJoined, msg.info);
                break;
            }
            case MsgType::PlayerLeft: {
                PlayerLeftMsg msg;
                if (!decode(data, size, msg)) {
                    ++stats.badPackets;
                    return;
                }
                auto it = remotes.find(msg.id);
                if (it == remotes.end()) return;
                const PlayerInfo info = it->second.info;
                remotes.erase(it);
                emit(EventType::PlayerLeft, info, msg.reason == LeaveReason::Quit ? "left" : "lost connection");
                break;
            }
            case MsgType::PlayerState: {
                PlayerStateMsg msg;
                if (!decode(data, size, msg)) {
                    ++stats.badPackets;
                    return;
                }
                if (msg.id == localId) return;
                auto it = remotes.find(msg.id);
                if (it == remotes.end()) return;  // state raced ahead of its PlayerJoined
                it->second.buffer.push(msg.state, now);
                it->second.lastStateAt = now;
                ++stats.statesReceived;
                break;
            }
            case MsgType::WorldState: {
                WorldStateMsg msg;
                if (!decode(data, size, msg)) {
                    ++stats.badPackets;
                    return;
                }
                world = msg.world;
                emit(EventType::WorldSync);
                break;
            }
            default:
                ++stats.badPackets;
                break;
        }
    }

    // ---------------------------------------------------------------- send

    void sendStateIfDue(int64_t now) {
        if (!hasLocal) return;
        const int64_t interval = std::max<int64_t>(1, 1000 / static_cast<int64_t>(cfg.sendRateHz));
        if (nextSendAt != 0 && now < nextSendAt) return;
        nextSendAt = (nextSendAt == 0 || now - nextSendAt > interval) ? now + interval : nextSendAt + interval;

        PlayerStateMsg msg;
        msg.id = localId;
        msg.state = local;
        msg.state.seq = ++seq;
        if (teleportSticky > 0) {
            msg.state.flags |= StateFlag::Teleported;
            --teleportSticky;
        }
        const std::vector<uint8_t> bytes = encode(msg);
        if (role == Role::Host) {
            for (const auto& kv : peerIds) sendBytes(kv.first, kChannelState, bytes, false, stats);
        } else {
            sendBytes(server, kChannelState, bytes, false, stats);
        }
        ++stats.statesSent;
    }

    void sendWorldIfDue(int64_t now) {
        if (now < nextWorldAt) return;
        nextWorldAt = now + cfg.worldSyncIntervalMs;
        if (peerIds.empty()) return;
        WorldStateMsg msg;
        msg.world = world;
        const std::vector<uint8_t> bytes = encode(msg);
        for (const auto& kv : peerIds) sendBytes(kv.first, kChannelControl, bytes, true, stats);
    }
};

// ------------------------------------------------------------------ Session

Session::Session(SessionSettings settings) : impl_(new Impl(std::move(settings))) {}
Session::~Session() = default;

bool Session::host(uint16_t port, std::string& error) { return impl_->startHost(port, error); }
bool Session::join(const std::string& address, uint16_t port, std::string& error) {
    return impl_->startClient(address, port, error);
}
void Session::leave() { impl_->leave(); }
void Session::update(int64_t nowMs) { impl_->update(nowMs); }

void Session::setLocalState(const PlayerState& state, int64_t nowMs) {
    Impl& s = *impl_;
    s.local = state;
    s.local.timeMs = static_cast<uint32_t>(nowMs);
    if ((state.flags & StateFlag::Teleported) != 0) s.teleportSticky = 3;
    s.local.flags &= static_cast<uint8_t>(~StateFlag::Teleported);
    s.hasLocal = true;
}

void Session::setWorldState(const WorldState& world) {
    if (impl_->role == Role::Host) impl_->world = world;
}

bool Session::pollEvent(Event& out) {
    if (impl_->events.empty()) return false;
    out = std::move(impl_->events.front());
    impl_->events.pop_front();
    return true;
}

Role Session::role() const { return impl_->role; }
Phase Session::phase() const { return impl_->phase; }
PlayerId Session::localId() const { return impl_->localId; }

int Session::playerCount() const {
    return impl_->phase == Phase::Active ? 1 + static_cast<int>(impl_->remotes.size()) : 0;
}

int Session::maxPlayers() const { return impl_->sessionMax; }

std::vector<PlayerId> Session::remoteIds() const {
    std::vector<PlayerId> ids;
    for (const auto& kv : impl_->remotes) ids.push_back(kv.first);
    return ids;
}

const PlayerInfo* Session::remoteInfo(PlayerId id) const {
    auto it = impl_->remotes.find(id);
    return it == impl_->remotes.end() ? nullptr : &it->second.info;
}

bool Session::sampleRemote(PlayerId id, int64_t nowMs, SampleResult& out) {
    auto it = impl_->remotes.find(id);
    if (it == impl_->remotes.end()) return false;
    return it->second.buffer.sample(nowMs, impl_->cfg.interp, out);
}

std::vector<RemoteView> Session::remoteViews(int64_t nowMs) const {
    std::vector<RemoteView> views;
    for (const auto& kv : impl_->remotes) {
        RemoteView v;
        v.info = kv.second.info;
        v.buffered = kv.second.buffer.size();
        v.msSinceLastState = kv.second.lastStateAt < 0 ? -1 : nowMs - kv.second.lastStateAt;
        if (impl_->role == Role::Host && kv.second.peer != nullptr) {
            v.pingMs = kv.second.peer->roundTripTime;
        } else if (impl_->role == Role::Client && kv.first == kHostId && impl_->server != nullptr) {
            v.pingMs = impl_->server->roundTripTime;
        }
        views.push_back(std::move(v));
    }
    return views;
}

uint32_t Session::hostPingMs() const {
    return (impl_->role == Role::Client && impl_->server != nullptr) ? impl_->server->roundTripTime : 0;
}

SessionStats Session::stats() const { return impl_->stats; }
void Session::setInterpSettings(const InterpSettings& s) { impl_->cfg.interp = s; }
const SessionSettings& Session::settings() const { return impl_->cfg; }

}  // namespace coop
