#pragma once

#include <cstddef>
#include <cstdint>

#include "status/StatusSnapshot.h"

namespace nova::detail {

/** Internal schema decoder used only behind StatusClient's public seam. */
PollError decodeStatusBody(const uint8_t* body, size_t length,
                           StatusSnapshot& snapshot);

}  // namespace nova::detail
