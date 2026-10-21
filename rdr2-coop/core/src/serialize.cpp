#include "coop/serialize.h"

#include <cstring>

namespace coop {

void ByteWriter::u16(uint16_t v) {
    u8(static_cast<uint8_t>(v & 0xFF));
    u8(static_cast<uint8_t>(v >> 8));
}

void ByteWriter::u32(uint32_t v) {
    u16(static_cast<uint16_t>(v & 0xFFFF));
    u16(static_cast<uint16_t>(v >> 16));
}

void ByteWriter::f32(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    u32(bits);
}

void ByteWriter::vec3(const Vec3& v) {
    f32(v.x);
    f32(v.y);
    f32(v.z);
}

void ByteWriter::str(const std::string& s, size_t maxLen) {
    if (maxLen > 255) maxLen = 255;
    const size_t n = s.size() < maxLen ? s.size() : maxLen;
    u8(static_cast<uint8_t>(n));
    buf_.insert(buf_.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
}

bool ByteReader::need(size_t n) {
    if (!ok_ || remaining() < n) {
        ok_ = false;
        return false;
    }
    return true;
}

uint8_t ByteReader::u8() {
    if (!need(1)) return 0;
    return *p_++;
}

uint16_t ByteReader::u16() {
    if (!need(2)) return 0;
    const uint16_t v = static_cast<uint16_t>(p_[0] | (p_[1] << 8));
    p_ += 2;
    return v;
}

uint32_t ByteReader::u32() {
    if (!need(4)) return 0;
    const uint32_t v = static_cast<uint32_t>(p_[0]) | (static_cast<uint32_t>(p_[1]) << 8) |
                       (static_cast<uint32_t>(p_[2]) << 16) | (static_cast<uint32_t>(p_[3]) << 24);
    p_ += 4;
    return v;
}

float ByteReader::f32() {
    const uint32_t bits = u32();
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

Vec3 ByteReader::vec3() {
    Vec3 v;
    v.x = f32();
    v.y = f32();
    v.z = f32();
    return v;
}

std::string ByteReader::str(size_t maxLen) {
    const size_t n = u8();
    if (!ok_) return {};
    if (n > maxLen) {
        ok_ = false;
        return {};
    }
    if (!need(n)) return {};
    std::string s(reinterpret_cast<const char*>(p_), n);
    p_ += n;
    return s;
}

}  // namespace coop
