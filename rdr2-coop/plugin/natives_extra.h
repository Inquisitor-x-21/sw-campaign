// Hand-written wrappers for variadic natives. The hashes still come from the
// generated header, so nothing here is typed by hand.
#pragma once

#include "natives_gen.h"

namespace rdr {
namespace MISC {

// Wraps a plain string so text natives (_BG_DISPLAY_TEXT, _SET_BLIP_NAME)
// accept it. Flags 10 keeps bit 0 clear, as the native requires.
inline const char* VAR_STRING_LITERAL(const char* text) {
    return invoke<const char*>(VAR_STRING_HASH, 10, "LITERAL_STRING", text);
}

}  // namespace MISC
}  // namespace rdr
