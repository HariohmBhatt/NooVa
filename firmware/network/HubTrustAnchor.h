#pragma once

#if __has_include("HubTrustAnchor.generated.h")
#include "HubTrustAnchor.generated.h"
#else
namespace nova {

// A development build without a generated trust anchor must fail closed.
inline constexpr char kHubRootCa[] = "";

}  // namespace nova
#endif
