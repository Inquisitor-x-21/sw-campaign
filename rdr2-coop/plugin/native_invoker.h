// Calls game natives through Script Hook RDR2 (nativeInit / nativePush64 /
// nativeCall from the SDK's main.h). Every argument is pushed as one 64-bit
// slot and results come back in 64-bit slots, the same way the SDK's own
// nativeCaller.h does it.
#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "main.h"  // Script Hook RDR2 SDK (sdk/inc/main.h)

namespace rdr {

using Any = int;
using Hash = uint32_t;
using Ped = int;
using Entity = int;
using Player = int;
using Vehicle = int;
using Object = int;
using Blip = int;

// Natives return Vector3 as three floats, each in its own 8-byte slot.
struct NVec3 {
    float x; uint32_t padX;
    float y; uint32_t padY;
    float z; uint32_t padZ;
};
static_assert(sizeof(NVec3) == 24, "NVec3 must match the native Vector3 layout");

template <typename T>
inline void pushArg(T value) {
    static_assert(sizeof(T) <= sizeof(uint64_t), "native arguments must fit in 8 bytes");
    static_assert(std::is_trivially_copyable<T>::value, "native arguments must be trivially copyable");
    uint64_t slot = 0;
    std::memcpy(&slot, &value, sizeof(T));
    nativePush64(slot);
}

template <typename R, typename... Args>
inline R invoke(uint64_t hash, Args... args) {
    nativeInit(hash);
    (pushArg(args), ...);
    if constexpr (std::is_void<R>::value) {
        nativeCall();
    } else {
        static_assert(std::is_trivially_copyable<R>::value, "native results must be trivially copyable");
        R result;
        std::memcpy(&result, reinterpret_cast<const void*>(nativeCall()), sizeof(R));
        return result;
    }
}

}  // namespace rdr
