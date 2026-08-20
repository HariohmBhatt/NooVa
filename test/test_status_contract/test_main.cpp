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

  bool take(nova::HttpsTransportEvent& event) override {
    if (failureReady_) {
      event.type = failureType_;
      failureReady_ = false;
      busy_ = false;
      return true;
    }
    if (!responseReady_) {
      return false;
    }
    if (responseOffset_ < responseBytes_.size()) {
      const size_t remaining = responseBytes_.size() - responseOffset_;
      event.type = nova::HttpsTransportEventType::ResponseBytes;
      event.bytes = reinterpret_cast<const uint8_t*>(responseBytes_.data()) +
                    responseOffset_;
      event.length = std::min(responseChunkSize_, remaining);
      responseOffset_ += event.length;
      return true;
    }
    event.type = nova::HttpsTransportEventType::ResponseComplete;
    responseReady_ = false;
    busy_ = false;
    return true;
  }

  void cancel() override {
    ++cancelCount_;
    busy_ = false;
    responseReady_ = false;
    failureReady_ = false;
  }

  void respond(std::string rawResponse,
               size_t chunkSize = nova::kMaxHttpsResponseChunkBytes) {
    responseBytes_ = std::move(rawResponse);
    responseOffset_ = 0;
    responseChunkSize_ = chunkSize;
    responseReady_ = true;
  }

  void fail(nova::HttpsTransportEventType type) {
    failureType_ = type;
    failureReady_ = true;
  }

  const nova::HttpsRequest& lastRequest() const { return lastRequest_; }
  size_t submitCount() const { return submitCount_; }
  size_t cancelCount() const { return cancelCount_; }

 private:
  nova::HttpsRequest lastRequest_{};
  std::string responseBytes_;
  nova::HttpsTransportEventType failureType_ =
      nova::HttpsTransportEventType::ConnectFailure;
  size_t responseOffset_ = 0;
  size_t responseChunkSize_ = nova::kMaxHttpsResponseChunkBytes;
  size_t submitCount_ = 0;
  size_t cancelCount_ = 0;
  bool busy_ = false;
  bool responseReady_ = false;
  bool failureReady_ = false;
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
                       const char* contentType = "application/json",
                       size_t chunkSize = nova::kMaxHttpsResponseChunkBytes) {
  nova::StatusClient client = makeClient();
  ScriptedTransport transport;
  nova::PollOutcome outcome{};
  TEST_ASSERT_FALSE(client.update(100, true, transport, outcome));
  transport.respond(response(body, status, extraHeaders, declaredLength,
                             contentType),
                    chunkSize);
  bool completed = false;
  for (uint32_t nowMs = 101; nowMs < 6101; ++nowMs) {
    if (client.update(nowMs, true, transport, outcome)) {
      completed = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(completed);
  return outcome;
}

uint32_t driveResponse(nova::StatusClient& client, ScriptedTransport& transport,
                       nova::PollOutcome& outcome, uint32_t firstUpdateAtMs) {
  for (uint32_t nowMs = firstUpdateAtMs; nowMs < firstUpdateAtMs + 6000;
       ++nowMs) {
    if (client.update(nowMs, true, transport, outcome)) {
      return nowMs;
    }
  }
  TEST_FAIL_MESSAGE("StatusClient did not complete the scripted response");
  return 0;
}

void replaceOne(std::string& value, const std::string& before,
                const std::string& after) {
  const size_t position = value.find(before);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, position);
  value.replace(position, before.size(), after);
}

std::string bodyWithExactSize(size_t targetSize) {
  const std::string prefix = "\"future_padding\":\"";
  const std::string suffix = "\",";
  std::string body = kValidJson;
  TEST_ASSERT_GREATER_OR_EQUAL(body.size() + prefix.size() + suffix.size(),
                               targetSize);
  const size_t fillerSize =
      targetSize - body.size() - prefix.size() - suffix.size();
  body.insert(body.find('{') + 1,
              prefix + std::string(fillerSize, 'p') + suffix);
  TEST_ASSERT_EQUAL_UINT(targetSize, body.size());
  return body;
}

std::string paddingHeaders(size_t byteCount) {
  std::string headers;
  while (byteCount > 0) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(4, byteCount);
    size_t lineBytes = std::min<size_t>(193, byteCount);
    const size_t remainder = byteCount - lineBytes;
    if (remainder > 0 && remainder < 4) {
      lineBytes -= 4 - remainder;
    }
    headers += "X:" + std::string(lineBytes - 4, 'p') + "\r\n";
    byteCount -= lineBytes;
  }
  return headers;
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
  driveResponse(client, transport, outcome, 101);
  TEST_ASSERT_EQUAL(nova::PollDisposition::Accepted, outcome.disposition);
  TEST_ASSERT_EQUAL_UINT32(42, outcome.snapshot.sequence);
  TEST_ASSERT_EQUAL_STRING("backup", outcome.snapshot.services[1].id);
}

void test_transport_failures_are_classified_through_client() {
  const std::pair<nova::HttpsTransportEventType, nova::PollError> cases[] = {
      {nova::HttpsTransportEventType::Timeout, nova::PollError::Timeout},
      {nova::HttpsTransportEventType::ConnectFailure,
       nova::PollError::DnsOrConnect},
      {nova::HttpsTransportEventType::TlsValidationFailure,
       nova::PollError::TlsValidation},
      {nova::HttpsTransportEventType::ResponseTooLarge,
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
    TEST_ASSERT_EQUAL(item.first == nova::HttpsTransportEventType::Timeout ||
                              item.first == nova::HttpsTransportEventType::ConnectFailure
                          ? nova::PollDisposition::TransientError
                          : nova::PollDisposition::MonitorError,
                      outcome.disposition);
  }
}

void test_response_is_ingested_incrementally_across_small_transport_chunks() {
  const nova::PollOutcome oneByte = poll(kValidJson, 200, {}, -1,
                                         "application/json", 1);
  TEST_ASSERT_EQUAL(nova::PollDisposition::Accepted, oneByte.disposition);

  const nova::PollOutcome sevenBytes = poll(kValidJson, 200, {}, -1,
                                            "application/json", 7);
  TEST_ASSERT_EQUAL(nova::PollDisposition::Accepted, sevenBytes.disposition);
  TEST_ASSERT_EQUAL_UINT32(42, sevenBytes.snapshot.sequence);
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
    const uint32_t completedAtMs =
        driveResponse(client, transport, outcome, 10);
    if (item.second > 0) {
      TEST_ASSERT_FALSE(client.update(completedAtMs + item.second - 1, true,
                                      transport, outcome));
      TEST_ASSERT_EQUAL_UINT(1, transport.submitCount());
    }
    TEST_ASSERT_FALSE(client.update(completedAtMs + item.second, true, transport,
                                    outcome));
    TEST_ASSERT_EQUAL_UINT(2, transport.submitCount());
  }

  nova::StatusClient hugeRetryClient = makeClient();
  ScriptedTransport hugeRetryTransport;
  TEST_ASSERT_FALSE(hugeRetryClient.update(0, true, hugeRetryTransport, outcome));
  hugeRetryTransport.respond(response(
      "{}", 429,
      "Retry-After: 999999999999999999999999999999999999999999\r\n"));
  const uint32_t hugeCompletedAtMs =
      driveResponse(hugeRetryClient, hugeRetryTransport, outcome, 10);
  TEST_ASSERT_FALSE(hugeRetryClient.update(
      hugeCompletedAtMs + 59999, true, hugeRetryTransport, outcome));
  TEST_ASSERT_FALSE(hugeRetryClient.update(
      hugeCompletedAtMs + 60000, true, hugeRetryTransport, outcome));
  TEST_ASSERT_EQUAL_UINT(2, hugeRetryTransport.submitCount());

  nova::StatusClient invalidRetryClient = makeClient();
  ScriptedTransport invalidRetryTransport;
  TEST_ASSERT_FALSE(
      invalidRetryClient.update(0, true, invalidRetryTransport, outcome));
  invalidRetryTransport.respond(
      response("{}", 429, "Retry-After: not-an-integer\r\n"));
  const uint32_t invalidCompletedAtMs =
      driveResponse(invalidRetryClient, invalidRetryTransport, outcome, 10);
  TEST_ASSERT_FALSE(invalidRetryClient.update(
      invalidCompletedAtMs + 4999, true, invalidRetryTransport, outcome));
  TEST_ASSERT_FALSE(invalidRetryClient.update(
      invalidCompletedAtMs + 5000, true, invalidRetryTransport, outcome));
  TEST_ASSERT_EQUAL_UINT(2, invalidRetryTransport.submitCount());

  nova::StatusClient normalClient = makeClient();
  ScriptedTransport normalTransport;
  TEST_ASSERT_FALSE(normalClient.update(100, true, normalTransport, outcome));
  normalTransport.respond(response(kValidJson));
  const uint32_t normalCompletedAtMs =
      driveResponse(normalClient, normalTransport, outcome, 200);
  TEST_ASSERT_FALSE(normalClient.update(normalCompletedAtMs + 4999, true,
                                        normalTransport, outcome));
  TEST_ASSERT_FALSE(normalClient.update(normalCompletedAtMs + 5000, true,
                                        normalTransport, outcome));
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

void test_maximum_valid_body_and_known_string_boundaries_are_accepted() {
  TEST_ASSERT_EQUAL(nova::PollDisposition::Accepted,
                    poll(bodyWithExactSize(4096)).disposition);

  std::string body = kValidJson;
  const std::string summary(64, 's');
  replaceOne(body, "\"summary\":\"System disk is 82% full\"",
             "\"summary\":\"" + summary + "\"");
  replaceOne(body, "\"message\":\"System disk is 82% full\"",
             "\"message\":\"" + summary + "\"");
  replaceOne(body, "\"code\":\"disk_high\"",
             "\"code\":\"" + std::string(32, 'c') + "\"");
  replaceOne(body, "\"id\":\"hub\"",
             "\"id\":\"" + std::string(24, 'i') + "\"");
  replaceOne(body, "\"name\":\"Hub\"",
             "\"name\":\"" + std::string(24, 'N') + "\"");
  TEST_ASSERT_EQUAL(nova::PollDisposition::Accepted, poll(body).disposition);
}

void test_each_overlong_known_string_is_rejected() {
  const std::pair<std::string, std::string> cases[] = {
      {"\"summary\":\"System disk is 82% full\"",
       "\"summary\":\"" + std::string(65, 's') + "\""},
      {"\"code\":\"disk_high\"",
       "\"code\":\"" + std::string(33, 'c') + "\""},
      {"\"message\":\"System disk is 82% full\"",
       "\"message\":\"" + std::string(97, 'm') + "\""},
      {"\"id\":\"hub\"",
       "\"id\":\"" + std::string(25, 'i') + "\""},
      {"\"name\":\"Hub\"",
       "\"name\":\"" + std::string(25, 'N') + "\""},
  };
  for (const auto& item : cases) {
    std::string body = kValidJson;
    replaceOne(body, item.first, item.second);
    TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, poll(body).error);
  }
}

void test_body_header_and_content_length_boundaries_are_enforced() {
  TEST_ASSERT_EQUAL(nova::PollError::OversizedBody,
                    poll(bodyWithExactSize(4097)).error);
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract,
                    poll(kValidJson, 200, {},
                         static_cast<int>(std::strlen(kValidJson)) - 1)
                        .error);
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract,
                    poll(kValidJson, 200, {},
                         static_cast<int>(std::strlen(kValidJson)) + 1)
                        .error);

  const std::string base = response(kValidJson);
  const size_t baseHeaderBytes = base.find("\r\n\r\n") + 4;
  TEST_ASSERT_LESS_THAN_UINT(1024, baseHeaderBytes);
  const std::string exactHeaders = paddingHeaders(1024 - baseHeaderBytes);
  const std::string exact = response(kValidJson, 200, exactHeaders);
  TEST_ASSERT_EQUAL_UINT(1024, exact.find("\r\n\r\n") + 4);
  TEST_ASSERT_EQUAL(nova::PollDisposition::Accepted,
                    poll(kValidJson, 200, exactHeaders).disposition);

  const std::string oversizedHeaders =
      paddingHeaders(1025 - baseHeaderBytes);
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract,
                    poll(kValidJson, 200, oversizedHeaders).error);

  nova::StatusClient client = makeClient();
  ScriptedTransport transport;
  nova::PollOutcome outcome{};
  TEST_ASSERT_FALSE(client.update(0, true, transport, outcome));
  transport.respond(std::string(nova::kMaxRawHttpResponseBytes + 1, 'x'));
  driveResponse(client, transport, outcome, 1);
  TEST_ASSERT_EQUAL(nova::PollError::OversizedBody, outcome.error);
}

void test_malformed_missing_counts_and_cross_field_invariants_are_rejected() {
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, poll("{").error);

  std::string missing = kValidJson;
  replaceOne(missing, "\"schema_version\":1,", "");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, poll(missing).error);

  const std::string service =
      "{\"id\":\"hub\",\"name\":\"Hub\",\"state\":\"healthy\"}";
  std::string tooManyServices = kValidJson;
  replaceOne(tooManyServices,
             service + ",{\"id\":\"backup\",\"name\":\"Backup\","
                       "\"state\":\"warning\"}",
             service +
                 ",{\"id\":\"s2\",\"name\":\"S2\",\"state\":\"healthy\"}"
                 ",{\"id\":\"s3\",\"name\":\"S3\",\"state\":\"healthy\"}"
                 ",{\"id\":\"s4\",\"name\":\"S4\",\"state\":\"healthy\"}"
                 ",{\"id\":\"s5\",\"name\":\"S5\",\"state\":\"healthy\"}");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract,
                    poll(tooManyServices).error);

  std::string healthyWithReasons = kValidJson;
  replaceOne(healthyWithReasons, "\"overall\":\"warning\"",
             "\"overall\":\"healthy\"");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract,
                    poll(healthyWithReasons).error);

  std::string nonhealthyWithoutReasons = kValidJson;
  const size_t reasonsStart = nonhealthyWithoutReasons.find("\"reasons\":[");
  const size_t reasonsEnd = nonhealthyWithoutReasons.find("],", reasonsStart);
  nonhealthyWithoutReasons.replace(reasonsStart, reasonsEnd - reasonsStart + 1,
                                   "\"reasons\":[]");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract,
                    poll(nonhealthyWithoutReasons).error);
}

void test_schema_types_ranges_counts_strings_and_invariants_are_rejected() {
  const std::pair<std::string, std::string> cases[] = {
      {"\"schema_version\":1", "\"schema_version\":2"},
      {"\"sequence\":42", "\"sequence\":-1"},
      {"\"overall\":\"warning\"", "\"overall\":\"amber\""},
      {"\"cpu_percent_tenths\":241", "\"cpu_percent_tenths\":1001"},
      {"\"memory_percent_tenths\":610", "\"memory_percent_tenths\":1001"},
      {"\"disk_percent_tenths\":821", "\"disk_percent_tenths\":1001"},
      {"\"uptime_seconds\":48210", "\"uptime_seconds\":-1"},
      {"\"generated_at_epoch_s\":1787227200",
       "\"generated_at_epoch_s\":-1"},
      {"\"sequence\":42", "\"sequence\":\"42\""},
      {"\"id\":\"hub\"", "\"id\":\"Not valid\""},
      {"\"code\":\"disk_high\"", "\"code\":\"Disk High\""},
      {"\"severity\":\"warning\"", "\"severity\":\"healthy\""},
      {"\"state\":\"healthy\"", "\"state\":\"offline\""},
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
  RUN_TEST(test_response_is_ingested_incrementally_across_small_transport_chunks);
  RUN_TEST(test_http_status_and_header_contract_are_classified);
  RUN_TEST(test_retry_after_caps_at_sixty_seconds_and_normal_poll_is_five_seconds);
  RUN_TEST(test_disconnect_cancels_inflight_and_reconnect_submits_immediately);
  RUN_TEST(test_null_unknown_and_body_bounds_are_validated_through_client);
  RUN_TEST(test_maximum_valid_body_and_known_string_boundaries_are_accepted);
  RUN_TEST(test_each_overlong_known_string_is_rejected);
  RUN_TEST(test_body_header_and_content_length_boundaries_are_enforced);
  RUN_TEST(test_malformed_missing_counts_and_cross_field_invariants_are_rejected);
  RUN_TEST(test_schema_types_ranges_counts_strings_and_invariants_are_rejected);
  RUN_TEST(test_overall_must_equal_highest_severity_across_all_reasons);
  return UNITY_END();
}
