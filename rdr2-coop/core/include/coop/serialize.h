#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "coop/mathutil.h"

namespace coop {

// Little-endian writer for network messages.
class ByteWriter {
public:
    void u8(uint8_t v) { buf_.push_back(v); }
    void u16(uint16_t v);
    void u32(uint32_t v);
    void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
    void f32(float v);
    void vec3(const Vec3& v);
    // u8 length prefix; longer strings are truncated to maxLen (max 255).
    void str(const std::string& s, size_t maxLen = 255);

    const std::vector<uint8_t>& data() const { return buf_; }
    std::vector<uint8_t> take() { return std::move(buf_); }

private:
    std::vector<uint8_t> buf_;
};

// Bounds-checked reader. Any read past the end, or a string longer than its
// limit, sets ok() to false and returns zero values, so decoders can read
// everything and check ok() once at the end.
class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : p_(data), end_(data + size) {}

    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    int16_t i16() { return static_cast<int16_t>(u16()); }
    float f32();
    Vec3 vec3();
    std::string str(size_t maxLen = 255);

    bool ok() const { return ok_; }
    size_t remaining() const { return static_cast<size_t>(end_ - p_); }
    void fail() { ok_ = false; }

private:
    bool need(size_t n);

    const uint8_t* p_;
    const uint8_t* end_;
    bool ok_ = true;
};

}  // namespace coop
