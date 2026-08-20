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
  TEST_ASSERT_TRUE(content.lastKnown);
  TEST_ASSERT_EQUAL_STRING("Connected", content.layers[0].status);
  TEST_ASSERT_EQUAL_STRING("No response", content.layers[1].status);
  TEST_ASSERT_EQUAL_STRING("Not evaluated", content.layers[2].status);
}

void test_presenter_never_formats_missing_metrics_as_zero() {
  nova::DeviceView view{};
  char output[16]{};
  nova::DashboardPresenter::formatMetric(view.snapshot.cpuPercentTenths, output,
                                         sizeof(output));
  TEST_ASSERT_EQUAL_STRING("Unavailable", output);
}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_presenter_maps_fresh_and_retained_states);
  RUN_TEST(test_presenter_never_formats_missing_metrics_as_zero);
  return UNITY_END();
}
