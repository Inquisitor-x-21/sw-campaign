#include "coop/protocol.h"

#include <cctype>
#include <cmath>

#include "coop/serialize.h"

namespace coop {
namespace {

ByteWriter begin(MsgType t) {
    ByteWriter w;
    w.u8(static_cast<uint8_t>(t));
    return w;
}

bool beginRead(ByteReader& r, MsgType expected) {
    return r.u8() == static_cast<uint8_t>(expected) && r.ok();
}

bool finished(const ByteReader& r) { return r.ok() && r.remaining() == 0; }

void writeInfo(ByteWriter& w, const PlayerInfo& p) {
    w.u8(p.id);
    w.str(p.name, kMaxNameLen);
    w.str(p.model, kMaxModelLen);
    w.i16(p.outfit);
}

PlayerInfo readInfo(ByteReader& r) {
    PlayerInfo p;
    p.id = r.u8();
    p.name = sanitizeName(r.str(kMaxNameLen));
    p.model = sanitizeModel(r.str(kMaxModelLen));
    p.outfit = r.i16();
    if (p.outfit < -1) r.fail();
    return p;
}

void writeWorld(ByteWriter& w, const WorldState& s) {
    w.u8(s.hour);
    w.u8(s.minute);
    w.u8(s.second);
}

WorldState readWorld(ByteReader& r) {
    WorldState s;
    s.hour = r.u8();
    s.minute = r.u8();
    s.second = r.u8();
    if (s.hour > 23 || s.minute > 59 || s.second > 59) r.fail();
    return s;
}

void writeState(ByteWriter& w, const PlayerState& s) {
    w.u32(s.timeMs);
    w.u16(s.seq);
    w.vec3(s.pos);
    w.vec3(s.vel);
    w.f32(s.heading);
    w.u8(static_cast<uint8_t>(s.move));
    w.u8(s.flags);
    if (s.mounted()) {
        w.u32(s.mountModel);
        w.vec3(s.mountPos);
        w.vec3(s.mountVel);
        w.f32(s.mountHeading);
    }
}

PlayerState readState(ByteReader& r) {
    PlayerState s;
    s.timeMs = r.u32();
    s.seq = r.u16();
    s.pos = r.vec3();
    s.vel = r.vec3();
    s.heading = r.f32();
    const uint8_t move = r.u8();
    s.flags = r.u8();
    if (move > static_cast<uint8_t>(MoveState::Sprint)) r.fail();
    s.move = static_cast<MoveState>(move);
    if (s.mounted()) {
        s.mountModel = r.u32();
        s.mountPos = r.vec3();
        s.mountVel = r.vec3();
        s.mountHeading = r.f32();
    }
    if (!isFinite(s.pos) || !isFinite(s.vel) || !std::isfinite(s.heading) || !isFinite(s.mountPos) ||
        !isFinite(s.mountVel) || !std::isfinite(s.mountHeading)) {
        r.fail();
    }
    s.heading = wrapDegrees(s.heading);
    s.mountHeading = wrapDegrees(s.mountHeading);
    return s;
}

}  // namespace

std::vector<uint8_t> encode(const HelloMsg& m) {
    ByteWriter w = begin(MsgType::Hello);
    w.u16(m.protocol);
    w.str(m.modVersion, kMaxVersionLen);
    writeInfo(w, m.info);
    return w.take();
}

std::vector<uint8_t> encode(const WelcomeMsg& m) {
    ByteWriter w = begin(MsgType::Welcome);
    w.u8(m.yourId);
    w.u8(m.maxPlayers);
    writeWorld(w, m.world);
    w.u8(static_cast<uint8_t>(m.players.size()));
    for (const PlayerInfo& p : m.players) writeInfo(w, p);
    return w.take();
}

std::vector<uint8_t> encode(const RejectMsg& m) {
    ByteWriter w = begin(MsgType::Reject);
    w.u8(static_cast<uint8_t>(m.reason));
    w.str(m.detail, kMaxDetailLen);
    return w.take();
}

std::vector<uint8_t> encode(const PlayerJoinedMsg& m) {
    ByteWriter w = begin(MsgType::PlayerJoined);
    writeInfo(w, m.info);
    return w.take();
}

std::vector<uint8_t> encode(const PlayerLeftMsg& m) {
    ByteWriter w = begin(MsgType::PlayerLeft);
    w.u8(m.id);
    w.u8(static_cast<uint8_t>(m.reason));
    return w.take();
}

std::vector<uint8_t> encode(const PlayerStateMsg& m) {
    ByteWriter w = begin(MsgType::PlayerState);
    w.u8(m.id);
    writeState(w, m.state);
    return w.take();
}

std::vector<uint8_t> encode(const WorldStateMsg& m) {
    ByteWriter w = begin(MsgType::WorldState);
    writeWorld(w, m.world);
    return w.take();
}

bool peekType(const uint8_t* data, size_t size, MsgType& out) {
    if (data == nullptr || size == 0) return false;
    out = static_cast<MsgType>(data[0]);
    return true;
}

bool decode(const uint8_t* data, size_t size, HelloMsg& out) {
    ByteReader r(data, size);
    if (!beginRead(r, MsgType::Hello)) return false;
    out.protocol = r.u16();
    out.modVersion = sanitizeText(r.str(kMaxVersionLen), kMaxVersionLen);
    out.info = readInfo(r);
    out.info.id = kNoPlayer;  // assigned by the host, never trusted from the client
    return finished(r);
}

bool decode(const uint8_t* data, size_t size, WelcomeMsg& out) {
    ByteReader r(data, size);
    if (!beginRead(r, MsgType::Welcome)) return false;
    out.yourId = r.u8();
    out.maxPlayers = r.u8();
    if (out.maxPlayers < 2 || out.maxPlayers > kMaxPlayersLimit) return false;
    out.world = readWorld(r);
    const uint8_t count = r.u8();
    if (count > kMaxPlayersLimit) return false;
    out.players.clear();
    for (uint8_t i = 0; i < count && r.ok(); ++i) out.players.push_back(readInfo(r));
    return finished(r) && out.yourId != kNoPlayer;
}

bool decode(const uint8_t* data, size_t size, RejectMsg& out) {
    ByteReader r(data, size);
    if (!beginRead(r, MsgType::Reject)) return false;
    const uint8_t reason = r.u8();
    if (reason < 1 || reason > 3) return false;
    out.reason = static_cast<RejectReason>(reason);
    out.detail = sanitizeText(r.str(kMaxDetailLen), kMaxDetailLen);
    return finished(r);
}

bool decode(const uint8_t* data, size_t size, PlayerJoinedMsg& out) {
    ByteReader r(data, size);
    if (!beginRead(r, MsgType::PlayerJoined)) return false;
    out.info = readInfo(r);
    return finished(r) && out.info.id != kNoPlayer;
}

bool decode(const uint8_t* data, size_t size, PlayerLeftMsg& out) {
    ByteReader r(data, size);
    if (!beginRead(r, MsgType::PlayerLeft)) return false;
    out.id = r.u8();
    const uint8_t reason = r.u8();
    if (reason > 1) return false;
    out.reason = static_cast<LeaveReason>(reason);
    return finished(r) && out.id != kNoPlayer;
}

bool decode(const uint8_t* data, size_t size, PlayerStateMsg& out) {
    ByteReader r(data, size);
    if (!beginRead(r, MsgType::PlayerState)) return false;
    out.id = r.u8();
    out.state = readState(r);
    return finished(r);
}

bool decode(const uint8_t* data, size_t size, WorldStateMsg& out) {
    ByteReader r(data, size);
    if (!beginRead(r, MsgType::WorldState)) return false;
    out.world = readWorld(r);
    return finished(r);
}

std::string sanitizeText(const std::string& s, size_t maxLen) {
    std::string out;
    for (char c : s) {
        if (c >= 0x20 && c <= 0x7E) out.push_back(c);
    }
    const size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    const size_t last = out.find_last_not_of(' ');
    out = out.substr(first, last - first + 1);
    if (out.size() > maxLen) out.resize(maxLen);
    return out;
}

std::string sanitizeModel(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') out.push_back(c);
        if (out.size() == kMaxModelLen) break;
    }
    return out;
}

uint32_t joaat(const std::string& s) {
    uint32_t h = 0;
    for (char ch : s) {
        h += static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(ch)));
        h += h << 10;
        h ^= h >> 6;
    }
    h += h << 3;
    h ^= h >> 11;
    h += h << 15;
    return h;
}

const char* toString(RejectReason r) {
    switch (r) {
        case RejectReason::VersionMismatch: return "mod version mismatch";
        case RejectReason::SessionFull: return "session is full";
        case RejectReason::BadHello: return "bad handshake";
    }
    return "unknown";
}

const char* toString(MoveState m) {
    switch (m) {
        case MoveState::Idle: return "idle";
        case MoveState::Walk: return "walk";
        case MoveState::Run: return "run";
        case MoveState::Sprint: return "sprint";
    }
    return "?";
}

}  // namespace coop
