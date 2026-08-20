#pragma once

#include <cstddef>

#include "status/SentinelModel.h"

namespace nova {

enum class UiTone : uint8_t { Neutral, Healthy, Warning, Critical, Stale, Error };

struct ConnectionLayerContent {
  const char* name;
  const char* status;
  const char* glyph;
};

/** Immutable, allocation-free copy deck consumed by the LVGL dashboard. */
struct DashboardContent {
  const char* title;
  const char* glyph;
  const char* fallbackSummary;
  UiTone tone;
  bool diagnostic;
  bool lastKnown;
  ConnectionLayerContent layers[3];
};

class DashboardPresenter {
 public:
  static DashboardContent present(const DeviceView& view);
  static void formatMetric(Nullable<uint16_t> metric, char* output,
                           size_t capacity);
  static const char* serviceGlyph(ServiceState state);
  static const char* serviceName(ServiceState state);
  static const char* monitorError(PollError error);
};

}  // namespace nova
