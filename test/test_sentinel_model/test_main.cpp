#include <unity.h>

#include <cstdint>
#include <cstring>

#include "status/SentinelModel.h"

namespace {

nova::StatusSnapshot snapshot(nova::ReportedSeverity severity) {
  nova::StatusSnapshot value{};
  value.sequence = 42;
  value.overall = severity;
  value.cpuPercentTenths = nova::Nullable<uint16_t>::withValue(241);
  std::strcpy(value.summary, "All monitored systems normal");
  return value;
}

void configureConnected(nova::SentinelModel& model, uint32_t now = 1000) {
  model.setConfigured(true, now);
  model.setWifiState(nova::WifiConnectionState::Connected, now);
}

void test_setup_and_connection_states_follow_precedence() {
  nova::SentinelModel model;
  TEST_ASSERT_EQUAL(nova::DeviceState::Starting, model.view(0).state);

  model.setConfigured(false, 1);
  TEST_ASSERT_EQUAL(nova::DeviceState::SetupRequired, model.view(1).state);

  model.setConfigured(true, 2);
  TEST_ASSERT_EQUAL(nova::DeviceState::WifiConnecting, model.view(2).state);
  model.setWifiState(nova::WifiConnectionState::Offline, 3);
  TEST_ASSERT_EQUAL(nova::DeviceState::WifiOffline, model.view(3).state);
  model.setWifiState(nova::WifiConnectionState::Connecting, 4);
  TEST_ASSERT_EQUAL(nova::DeviceState::WifiConnecting, model.view(4).state);
  model.setWifiState(nova::WifiConnectionState::Connected, 5);
  TEST_ASSERT_EQUAL(nova::DeviceState::ServerConnecting, model.view(5).state);
  TEST_ASSERT_EQUAL(nova::DeviceState::ServerConnecting, model.view(10004).state);
  TEST_ASSERT_EQUAL(nova::DeviceState::ServerOffline, model.view(10005).state);
}

void test_accepted_snapshot_reports_severity_and_retains_values() {
  nova::SentinelModel model;
  configureConnected(model);
  model.apply(nova::PollOutcome::accepted(snapshot(nova::ReportedSeverity::Warning)),
              2000);

  const nova::DeviceView view = model.view(2000);
  TEST_ASSERT_EQUAL(nova::DeviceState::Warning, view.state);
  TEST_ASSERT_TRUE(view.hasSnapshot);
  TEST_ASSERT_FALSE(view.lastKnown);
  TEST_ASSERT_TRUE(view.snapshot.cpuPercentTenths.available);
  TEST_ASSERT_EQUAL_UINT16(241, view.snapshot.cpuPercentTenths.value);
}

void test_freshness_boundaries_and_recovery_are_exact() {
  nova::SentinelModel model;
  configureConnected(model, 100);
  model.apply(nova::PollOutcome::accepted(snapshot(nova::ReportedSeverity::Healthy)),
              1000);

  TEST_ASSERT_EQUAL(nova::DeviceState::Healthy, model.view(15999).state);
  TEST_ASSERT_EQUAL(nova::DeviceState::Stale, model.view(16000).state);
  TEST_ASSERT_TRUE(model.view(16000).lastKnown);
  TEST_ASSERT_EQUAL(nova::DeviceState::Stale, model.view(30999).state);
  TEST_ASSERT_EQUAL(nova::DeviceState::ServerOffline, model.view(31000).state);

  model.apply(nova::PollOutcome::accepted(snapshot(nova::ReportedSeverity::Critical)),
              32000);
  TEST_ASSERT_EQUAL(nova::DeviceState::Critical, model.view(32000).state);
  TEST_ASSERT_FALSE(model.view(32000).lastKnown);
}

void test_wifi_and_monitor_error_precedence_do_not_erase_snapshot() {
  nova::SentinelModel model;
  configureConnected(model);
  model.apply(nova::PollOutcome::accepted(snapshot(nova::ReportedSeverity::Healthy)),
              2000);
  model.apply(nova::PollOutcome::monitorError(nova::PollError::Authentication),
              2100);
  TEST_ASSERT_EQUAL(nova::DeviceState::MonitorError, model.view(2100).state);
  TEST_ASSERT_TRUE(model.view(2100).lastKnown);

  model.setWifiState(nova::WifiConnectionState::Offline, 2200);
  TEST_ASSERT_EQUAL(nova::DeviceState::WifiOffline, model.view(2200).state);
  TEST_ASSERT_TRUE(model.view(2200).hasSnapshot);

  model.setWifiState(nova::WifiConnectionState::Connected, 2300);
  TEST_ASSERT_EQUAL(nova::DeviceState::MonitorError, model.view(2300).state);
  model.apply(nova::PollOutcome::accepted(snapshot(nova::ReportedSeverity::Warning)),
              2400);
  TEST_ASSERT_EQUAL(nova::DeviceState::Warning, model.view(2400).state);
}

void test_transient_failures_advance_freshness_only() {
  nova::SentinelModel model;
  configureConnected(model);
  model.apply(nova::PollOutcome::accepted(snapshot(nova::ReportedSeverity::Healthy)),
              1000);
  model.apply(nova::PollOutcome::transientError(nova::PollError::Timeout), 2000);
  TEST_ASSERT_EQUAL(nova::DeviceState::Healthy, model.view(2000).state);
  TEST_ASSERT_EQUAL(nova::DeviceState::Stale, model.view(16000).state);
}

void test_uint32_rollover_is_safe() {
  nova::SentinelModel model;
  constexpr uint32_t acceptedAt = UINT32_MAX - 9999U;
  configureConnected(model, acceptedAt - 100U);
  model.apply(nova::PollOutcome::accepted(snapshot(nova::ReportedSeverity::Healthy)),
              acceptedAt);
  TEST_ASSERT_EQUAL_UINT32(14999U, model.view(4999U).snapshotAgeMs);
  TEST_ASSERT_EQUAL(nova::DeviceState::Healthy, model.view(4999U).state);
  TEST_ASSERT_EQUAL(nova::DeviceState::Stale, model.view(5000U).state);
  TEST_ASSERT_EQUAL(nova::DeviceState::ServerOffline, model.view(20000U).state);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_setup_and_connection_states_follow_precedence);
  RUN_TEST(test_accepted_snapshot_reports_severity_and_retains_values);
  RUN_TEST(test_freshness_boundaries_and_recovery_are_exact);
  RUN_TEST(test_wifi_and_monitor_error_precedence_do_not_erase_snapshot);
  RUN_TEST(test_transient_failures_advance_freshness_only);
  RUN_TEST(test_uint32_rollover_is_safe);
  return UNITY_END();
}
