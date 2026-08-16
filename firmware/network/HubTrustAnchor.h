#pragma once

#if __has_include("HubTrustAnchor.generated.h")
#include "HubTrustAnchor.generated.h"
#else
namespace nova {

/** Empty trust anchor used by development builds without generated CA data. */
inline constexpr char kHubRootCa[] = "";

}  // namespace nova
#endif
