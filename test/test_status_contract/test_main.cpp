#include <unity.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "network/StatusClient.h"

namespace {

constexpr char kValidJson[] = R"json({
  "schema_version":1,
  "sequence":42,
  "generated_at_epoch_s":1787227200,
  "overall":"warning",
  "summary":"System disk is 82% full",
  "reasons":[{"code":"disk_high","severity":"warning","message":"System disk is 82% full"}],
  "metrics":{"cpu_percent_tenths":241,"memory_percent_tenths":610,"disk_percent_tenths":821,"uptime_seconds":48210},
  "services":[{"id":"hub","name":"Hub","state":"healthy"},{"id":"backup","name":"Backup","state":"warning"}]
})json";

class ScriptedTransport final : public nova::StatusTransport {
 public:
  bool submit(const nova::HttpsRequest& request) override {
    if (busy_) {
      return false;
    }
    lastRequest_ = request;
    ++submitCount_;
    busy_ = true;
    return true;
  }

  bool take(nova::HttpsResponseView& response) override {
    if (!responseReady_) {
      return false;
    }
    response.status = responseStatus_;
    response.bytes = reinterpret_cast<const uint8_t*>(responseBytes_.data());
    response.length = responseBytes_.size();
    responseReady_ = false;
    busy_ = false;
    return true;
  }

  void cancel() override {
    ++cancelCount_;
    busy_ = false;
    responseReady_ = false;
  }

  void respond(std::string rawResponse) {
    responseBytes_ = std::move(rawResponse);
    responseStatus_ = nova::HttpsTransportStatus::Complete;
    responseReady_ = true;
  }

  void fail(nova::HttpsTransportStatus status) {
    responseBytes_.clear();
    responseStatus_ = status;
    responseReady_ = true;
  }

  const nova::HttpsRequest& lastRequest() const { return lastRequest_; }
  size_t submitCount() const { return submitCount_; }
  size_t cancelCount() const { return cancelCount_; }

 private:
  nova::HttpsRequest lastRequest_{};
  std::string responseBytes_;
  nova::HttpsTransportStatus responseStatus_ = nova::HttpsTransportStatus::Complete;
  size_t submitCount_ = 0;
  size_t cancelCount_ = 0;
  bool busy_ = false;
  bool responseReady_ = false;
};

nova::StatusClient makeClient() {
  nova::StatusClient client;
  const nova::StatusClientConfig config{"nova-sentinel.local", "/v1/status",
                                        "test-device-token"};
  TEST_ASSERT_TRUE(client.begin(config));
  return client;
}

std::string response(const std::string& body, int status = 200,
                     const std::string& extraHeaders = {},
                     int declaredLength = -1,
                     const char* contentType = "application/json") {
  const size_t length = declaredLength < 0 ? body.size()
                                           : static_cast<size_t>(declaredLength);
  return "HTTP/1.1 " + std::to_string(status) + " Status\r\n" +
         "Content-Type: " + contentType + "\r\n" +
         "Content-Length: " + std::to_string(length) + "\r\n" +
         extraHeaders + "\r\n" + body;
}

nova::PollOutcome poll(const std::string& body, int status = 200,
                       const std::string& extraHeaders = {},
                       int declaredLength = -1,
                       const char* contentType = "application/json") {
  nova::StatusClient client = makeClient();
  ScriptedTransport transport;
  nova::PollOutcome outcome{};
  TEST_ASSERT_FALSE(client.update(100, true, transport, outcome));
  transport.respond(response(body, status, extraHeaders, declaredLength,
                             contentType));
  TEST_ASSERT_TRUE(client.update(101, true, transport, outcome));
  return outcome;
}

void replaceOne(std::string& value, const std::string& before,
                const std::string& after) {
  const size_t position = value.find(before);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, position);
  value.replace(position, before.size(), after);
}

void test_client_generates_exact_authenticated_request_and_accepts_response() {
  nova::StatusClient client = makeClient();
  ScriptedTransport transport;
  nova::PollOutcome outcome{};

  TEST_ASSERT_FALSE(client.update(100, true, transport, outcome));
  TEST_ASSERT_EQUAL_STRING(
      "GET /v1/status HTTP/1.1\r\n"
      "Host: nova-sentinel.local\r\n"
      "Accept: application/json\r\n"
      "Authorization: Bearer test-device-token\r\n"
      "X-Nova-Schema: 1\r\n"
      "Connection: close\r\n\r\n",
      transport.lastRequest().bytes);

  transport.respond(response(kValidJson));
  TEST_ASSERT_TRUE(client.update(101, true, transport, outcome));
  TEST_ASSERT_EQUAL(nova::PollDisposition::Accepted, outcome.disposition);
  TEST_ASSERT_EQUAL_UINT32(42, outcome.snapshot.sequence);
  TEST_ASSERT_EQUAL_STRING("backup", outcome.snapshot.services[1].id);
}

void test_transport_failures_are_classified_through_client() {
  const std::pair<nova::HttpsTransportStatus, nova::PollError> cases[] = {
      {nova::HttpsTransportStatus::Timeout, nova::PollError::Timeout},
      {nova::HttpsTransportStatus::ConnectFailure,
       nova::PollError::DnsOrConnect},
      {nova::HttpsTransportStatus::TlsValidationFailure,
       nova::PollError::TlsValidation},
      {nova::HttpsTransportStatus::ResponseTooLarge,
       nova::PollError::OversizedBody},
  };
  for (const auto& item : cases) {
    nova::StatusClient client = makeClient();
    ScriptedTransport transport;
    nova::PollOutcome outcome{};
    TEST_ASSERT_FALSE(client.update(0, true, transport, outcome));
    transport.fail(item.first);
    TEST_ASSERT_TRUE(client.update(1, true, transport, outcome));
    TEST_ASSERT_EQUAL(item.second, outcome.error);
    TEST_ASSERT_EQUAL(item.first == nova::HttpsTransportStatus::Timeout ||
                              item.first == nova::HttpsTransportStatus::ConnectFailure
                          ? nova::PollDisposition::TransientError
                          : nova::PollDisposition::MonitorError,
                      outcome.disposition);
  }
}

void test_http_status_and_header_contract_are_classified() {
  TEST_ASSERT_EQUAL(nova::PollError::Authentication, poll("{}", 401).error);
  TEST_ASSERT_EQUAL(nova::PollError::Authentication, poll("{}", 403).error);
  TEST_ASSERT_EQUAL(nova::PollError::UnsupportedSchema, poll("{}", 426).error);
  TEST_ASSERT_EQUAL(nova::PollDisposition::TransientError,
                    poll("{}", 429).disposition);
  TEST_ASSERT_EQUAL(nova::PollDisposition::TransientError,
                    poll("{}", 503).disposition);
  TEST_ASSERT_EQUAL(nova::PollError::UnexpectedHttpStatus, poll("{}", 404).error);
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract,
                    poll(kValidJson, 200, {}, -1, "text/plain").error);
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract,
                    poll(kValidJson, 200, "Transfer-Encoding: chunked\r\n").error);
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract,
                    poll(kValidJson, 200, "Content-Length: 7\r\n").error);
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract,
                    poll(kValidJson, 200, {}, 7).error);
}

void test_retry_after_caps_at_sixty_seconds_and_normal_poll_is_five_seconds() {
  nova::PollOutcome outcome{};
  const std::pair<unsigned, uint32_t> retryCases[] = {
      {0, 0}, {59, 59000}, {60, 60000}, {61, 60000}};
  for (const auto& item : retryCases) {
    nova::StatusClient client = makeClient();
    ScriptedTransport transport;
    TEST_ASSERT_FALSE(client.update(0, true, transport, outcome));
    transport.respond(response(
        "{}", 429, "Retry-After: " + std::to_string(item.first) + "\r\n"));
    TEST_ASSERT_TRUE(client.update(10, true, transport, outcome));
    if (item.second > 0) {
      TEST_ASSERT_FALSE(
          client.update(10 + item.second - 1, true, transport, outcome));
      TEST_ASSERT_EQUAL_UINT(1, transport.submitCount());
    }
    TEST_ASSERT_FALSE(
        client.update(10 + item.second, true, transport, outcome));
    TEST_ASSERT_EQUAL_UINT(2, transport.submitCount());
  }

  nova::StatusClient hugeRetryClient = makeClient();
  ScriptedTransport hugeRetryTransport;
  TEST_ASSERT_FALSE(hugeRetryClient.update(0, true, hugeRetryTransport, outcome));
  hugeRetryTransport.respond(response(
      "{}", 429,
      "Retry-After: 999999999999999999999999999999999999999999\r\n"));
  TEST_ASSERT_TRUE(hugeRetryClient.update(10, true, hugeRetryTransport, outcome));
  TEST_ASSERT_FALSE(
      hugeRetryClient.update(60009, true, hugeRetryTransport, outcome));
  TEST_ASSERT_FALSE(
      hugeRetryClient.update(60010, true, hugeRetryTransport, outcome));
  TEST_ASSERT_EQUAL_UINT(2, hugeRetryTransport.submitCount());

  nova::StatusClient invalidRetryClient = makeClient();
  ScriptedTransport invalidRetryTransport;
  TEST_ASSERT_FALSE(
      invalidRetryClient.update(0, true, invalidRetryTransport, outcome));
  invalidRetryTransport.respond(
      response("{}", 429, "Retry-After: not-an-integer\r\n"));
  TEST_ASSERT_TRUE(
      invalidRetryClient.update(10, true, invalidRetryTransport, outcome));
  TEST_ASSERT_FALSE(
      invalidRetryClient.update(5009, true, invalidRetryTransport, outcome));
  TEST_ASSERT_FALSE(
      invalidRetryClient.update(5010, true, invalidRetryTransport, outcome));
  TEST_ASSERT_EQUAL_UINT(2, invalidRetryTransport.submitCount());

  nova::StatusClient normalClient = makeClient();
  ScriptedTransport normalTransport;
  TEST_ASSERT_FALSE(normalClient.update(100, true, normalTransport, outcome));
  normalTransport.respond(response(kValidJson));
  TEST_ASSERT_TRUE(normalClient.update(200, true, normalTransport, outcome));
  TEST_ASSERT_FALSE(normalClient.update(5199, true, normalTransport, outcome));
  TEST_ASSERT_FALSE(normalClient.update(5200, true, normalTransport, outcome));
  TEST_ASSERT_EQUAL_UINT(2, normalTransport.submitCount());
}

void test_disconnect_cancels_inflight_and_reconnect_submits_immediately() {
  nova::StatusClient client = makeClient();
  ScriptedTransport transport;
  nova::PollOutcome outcome{};
  TEST_ASSERT_FALSE(client.update(0, true, transport, outcome));
  TEST_ASSERT_FALSE(client.update(1, false, transport, outcome));
  TEST_ASSERT_EQUAL_UINT(1, transport.cancelCount());
  TEST_ASSERT_FALSE(client.update(2, true, transport, outcome));
  TEST_ASSERT_EQUAL_UINT(2, transport.submitCount());
}

void test_null_unknown_and_body_bounds_are_validated_through_client() {
  std::string body = kValidJson;
  replaceOne(body, "\"cpu_percent_tenths\":241",
             "\"cpu_percent_tenths\":null");
  replaceOne(body, "\"services\":[",
             "\"future\":{\"large\":[1,2,3]},\"services\":[");
  const nova::PollOutcome accepted = poll(body);
  TEST_ASSERT_EQUAL(nova::PollDisposition::Accepted, accepted.disposition);
  TEST_ASSERT_FALSE(accepted.snapshot.cpuPercentTenths.available);

  TEST_ASSERT_EQUAL(nova::PollError::OversizedBody,
                    poll(std::string(4097, 'x')).error);

  std::string badUnknownUtf8 = kValidJson;
  replaceOne(badUnknownUtf8, "\"services\":[",
             std::string("\"future\":\"") + char(0xC3) + "\",\"services\":[");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, poll(badUnknownUtf8).error);
}

void test_schema_types_ranges_counts_strings_and_invariants_are_rejected() {
  const std::pair<std::string, std::string> cases[] = {
      {"\"schema_version\":1", "\"schema_version\":2"},
      {"\"sequence\":42", "\"sequence\":-1"},
      {"\"overall\":\"warning\"", "\"overall\":\"amber\""},
      {"\"cpu_percent_tenths\":241", "\"cpu_percent_tenths\":1001"},
      {"\"sequence\":42", "\"sequence\":\"42\""},
      {"\"id\":\"hub\"", "\"id\":\"Not valid\""},
      {"\"summary\":\"System disk is 82% full\"",
       "\"summary\":\"Different\""},
      {"\"id\":\"backup\"", "\"id\":\"hub\""},
  };
  for (const auto& item : cases) {
    std::string body = kValidJson;
    replaceOne(body, item.first, item.second);
    TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, poll(body).error);
  }

  std::string tooManyReasons = kValidJson;
  const std::string reason =
      "{\"code\":\"disk_high\",\"severity\":\"warning\",\"message\":\"System disk is 82% full\"}";
  replaceOne(tooManyReasons, reason,
             reason + "," + reason + "," + reason + "," + reason);
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, poll(tooManyReasons).error);
}

void test_overall_must_equal_highest_severity_across_all_reasons() {
  std::string body = kValidJson;
  replaceOne(
      body,
      "{\"code\":\"disk_high\",\"severity\":\"warning\",\"message\":\"System disk is 82% full\"}",
      "{\"code\":\"disk_high\",\"severity\":\"warning\",\"message\":\"System disk is 82% full\"},"
      "{\"code\":\"service_down\",\"severity\":\"critical\",\"message\":\"Critical service is down\"}");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, poll(body).error);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_client_generates_exact_authenticated_request_and_accepts_response);
  RUN_TEST(test_transport_failures_are_classified_through_client);
  RUN_TEST(test_http_status_and_header_contract_are_classified);
  RUN_TEST(test_retry_after_caps_at_sixty_seconds_and_normal_poll_is_five_seconds);
  RUN_TEST(test_disconnect_cancels_inflight_and_reconnect_submits_immediately);
  RUN_TEST(test_null_unknown_and_body_bounds_are_validated_through_client);
  RUN_TEST(test_schema_types_ranges_counts_strings_and_invariants_are_rejected);
  RUN_TEST(test_overall_must_equal_highest_severity_across_all_reasons);
  return UNITY_END();
}
