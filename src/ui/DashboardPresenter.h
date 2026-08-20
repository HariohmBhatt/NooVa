#pragma once

#include <cstddef>

#include "status/SentinelModel.h"

namespace nova {

enum class UiTone : uint8_t { Neutral, Healthy, Warning, Critical, Stale, Error };

struct ConnectionLayerContent {
  const char* name;
  const char* status;
  const char* glyph;
  UiTone tone;
};

struct ServiceContent {
  const char* glyph;
  const char* name;
  UiTone tone;
};

/** Immutable, allocation-free copy deck consumed by the LVGL dashboard. */
struct DashboardContent {
  const char* title;
  const char* glyph;
  const char* fallbackSummary;
  UiTone tone;
  bool diagnostic;
  ConnectionLayerContent layers[3];
};

class DashboardPresenter {
 public:
  static DashboardContent present(const DeviceView& view);
  static void formatFreshness(const DeviceView& view, char* output,
                              size_t capacity);
  static void formatMetric(Nullable<uint16_t> metric, char* output,
                           size_t capacity);
  static void formatUptime(Nullable<uint32_t> uptime, char* output,
                           size_t capacity);
  static ServiceContent service(ServiceState state);
  static const char* monitorError(PollError error);
};

}  // namespace nova
