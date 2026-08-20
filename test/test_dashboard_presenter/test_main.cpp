#include <unity.h>

#include "status/SentinelModel.h"
#include "ui/DashboardPresenter.h"

void test_presenter_maps_fresh_and_retained_states() {
  nova::DeviceView view{};
  view.state = nova::DeviceState::Critical;
  auto content = nova::DashboardPresenter::present(view);
  TEST_ASSERT_EQUAL_STRING("Critical", content.title);
  TEST_ASSERT_EQUAL_STRING("X", content.glyph);
  TEST_ASSERT_FALSE(content.diagnostic);

  view.state = nova::DeviceState::ServerOffline;
  view.lastKnown = true;
  content = nova::DashboardPresenter::present(view);
  TEST_ASSERT_EQUAL_STRING("Server offline", content.title);
  TEST_ASSERT_TRUE(content.diagnostic);
  TEST_ASSERT_EQUAL_STRING("Connected", content.layers[0].status);
  TEST_ASSERT_EQUAL_STRING("No response", content.layers[1].status);
  TEST_ASSERT_EQUAL_STRING("Not evaluated", content.layers[2].status);

  view.state = nova::DeviceState::Healthy;
  view.lastKnown = false;
  content = nova::DashboardPresenter::present(view);
  TEST_ASSERT_EQUAL_STRING("Connected", content.layers[0].status);
  TEST_ASSERT_EQUAL_STRING("Reachable", content.layers[1].status);
  TEST_ASSERT_EQUAL_STRING("Validated", content.layers[2].status);

  const auto unknown =
      nova::DashboardPresenter::service(nova::ServiceState::Unknown);
  TEST_ASSERT_EQUAL_STRING("-", unknown.glyph);
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", unknown.name);
  TEST_ASSERT_EQUAL(nova::UiTone::Neutral, unknown.tone);
}

void test_presenter_never_formats_missing_metrics_as_zero() {
  nova::DeviceView view{};
  char output[16]{};
  nova::DashboardPresenter::formatMetric(view.snapshot.cpuPercentTenths, output,
                                         sizeof(output));
  TEST_ASSERT_EQUAL_STRING("Unavailable", output);

  char freshness[32]{};
  nova::DashboardPresenter::formatFreshness(view, freshness,
                                             sizeof(freshness));
  TEST_ASSERT_EQUAL_STRING("STARTING", freshness);
  view.state = nova::DeviceState::ServerConnecting;
  nova::DashboardPresenter::formatFreshness(view, freshness,
                                             sizeof(freshness));
  TEST_ASSERT_EQUAL_STRING("WAITING FOR SERVER", freshness);
  view.state = nova::DeviceState::Healthy;
  view.hasSnapshot = true;
  view.snapshotAgeMs = 1234;
  nova::DashboardPresenter::formatFreshness(view, freshness,
                                             sizeof(freshness));
  TEST_ASSERT_EQUAL_STRING("LIVE | 1s", freshness);
  view.state = nova::DeviceState::Stale;
  view.lastKnown = true;
  view.snapshotAgeMs = 22000;
  nova::DashboardPresenter::formatFreshness(view, freshness,
                                             sizeof(freshness));
  TEST_ASSERT_EQUAL_STRING("LAST KNOWN | 22s", freshness);

  char uptime[24]{};
  nova::DashboardPresenter::formatUptime(
      nova::Nullable<uint32_t>::withValue(93784U), uptime, sizeof(uptime));
  TEST_ASSERT_EQUAL_STRING("1d 2h 3m", uptime);
  nova::DashboardPresenter::formatUptime({}, uptime, sizeof(uptime));
  TEST_ASSERT_EQUAL_STRING("Unavailable", uptime);
}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_presenter_maps_fresh_and_retained_states);
  RUN_TEST(test_presenter_never_formats_missing_metrics_as_zero);
  return UNITY_END();
}
