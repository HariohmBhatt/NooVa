#include <unity.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "network/StatusCodec.h"

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

nova::PollOutcome decode(const std::string& body, int status = 200,
                         const char* contentType = "application/json",
                         int32_t declaredLength = -2, size_t chunkSize = 0) {
  nova::HttpResponseMetadata metadata{};
  metadata.statusCode = status;
  metadata.contentLength = declaredLength == -2
                               ? static_cast<int32_t>(body.size())
                               : declaredLength;
  std::strncpy(metadata.contentType, contentType, sizeof(metadata.contentType) - 1);
  nova::StatusDecoder decoder(metadata);
  const size_t stride = chunkSize == 0 ? body.size() : chunkSize;
  for (size_t offset = 0; offset < body.size(); offset += stride) {
    const size_t count = std::min(stride, body.size() - offset);
    decoder.append(reinterpret_cast<const uint8_t*>(body.data() + offset), count);
  }
  return decoder.finish();
}

void replaceOne(std::string& value, const std::string& before,
                const std::string& after) {
  const size_t position = value.find(before);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, position);
  value.replace(position, before.size(), after);
}

void test_valid_contract_decodes_when_delivered_in_small_chunks() {
  const nova::PollOutcome outcome = decode(kValidJson, 200, "application/json", -2, 3);
  TEST_ASSERT_EQUAL(nova::PollDisposition::Accepted, outcome.disposition);
  TEST_ASSERT_EQUAL_UINT32(42, outcome.snapshot.sequence);
  TEST_ASSERT_EQUAL(nova::ReportedSeverity::Warning, outcome.snapshot.overall);
  TEST_ASSERT_EQUAL_UINT8(1, outcome.snapshot.reasonCount);
  TEST_ASSERT_EQUAL_UINT8(2, outcome.snapshot.serviceCount);
  TEST_ASSERT_EQUAL_STRING("backup", outcome.snapshot.services[1].id);
}

void test_null_metrics_remain_unavailable_and_unknown_fields_are_ignored() {
  std::string body = kValidJson;
  replaceOne(body, "\"cpu_percent_tenths\":241", "\"cpu_percent_tenths\":null");
  replaceOne(body, "\"services\":[", "\"future\":{\"large\":[1,2,3]},\"services\":[");
  const nova::PollOutcome outcome = decode(body);
  TEST_ASSERT_EQUAL(nova::PollDisposition::Accepted, outcome.disposition);
  TEST_ASSERT_FALSE(outcome.snapshot.cpuPercentTenths.available);
  TEST_ASSERT_TRUE(outcome.snapshot.memoryPercentTenths.available);
}

void test_http_and_transport_contract_errors_are_classified() {
  TEST_ASSERT_EQUAL(nova::PollError::Authentication,
                    decode("{}", 401).error);
  TEST_ASSERT_EQUAL(nova::PollError::Authentication,
                    decode("{}", 403).error);
  TEST_ASSERT_EQUAL(nova::PollError::UnsupportedSchema,
                    decode("{}", 426).error);
  TEST_ASSERT_EQUAL(nova::PollDisposition::TransientError,
                    decode("{}", 429).disposition);
  TEST_ASSERT_EQUAL(nova::PollDisposition::TransientError,
                    decode("{}", 503).disposition);
  TEST_ASSERT_EQUAL(nova::PollError::UnexpectedHttpStatus,
                    decode("{}", 404).error);
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract,
                    decode(kValidJson, 200, "text/plain").error);
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract,
                    decode(kValidJson, 200, "application/json", -1).error);
}

void test_body_and_declared_length_are_strictly_bounded() {
  const std::string oversized(4097, 'x');
  TEST_ASSERT_EQUAL(nova::PollError::OversizedBody, decode(oversized).error);
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract,
                    decode(kValidJson, 200, "application/json", 7).error);
}

void test_schema_enum_and_numeric_violations_reject_whole_snapshot() {
  const std::pair<std::string, std::string> cases[] = {
      {"\"schema_version\":1", "\"schema_version\":2"},
      {"\"sequence\":42", "\"sequence\":-1"},
      {"\"overall\":\"warning\"", "\"overall\":\"amber\""},
      {"\"cpu_percent_tenths\":241", "\"cpu_percent_tenths\":1001"},
      {"\"uptime_seconds\":48210", "\"uptime_seconds\":-1"},
      {"\"state\":\"healthy\"", "\"state\":\"offline\""},
  };
  for (const auto& item : cases) {
    std::string body = kValidJson;
    replaceOne(body, item.first, item.second);
    TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(body).error);
  }
}

void test_missing_wrong_type_and_malformed_fields_reject_snapshot() {
  const std::pair<std::string, std::string> cases[] = {
      {"\"sequence\":42,", ""},
      {"\"sequence\":42", "\"sequence\":\"42\""},
      {"\"metrics\":{", "\"metrics_missing\":{"},
      {"\"reasons\":[", "\"reasons\":{},\"ignored\":["},
  };
  for (const auto& item : cases) {
    std::string body = kValidJson;
    replaceOne(body, item.first, item.second);
    TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(body).error);
  }
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode("{broken").error);
}

void test_count_string_identifier_and_utf8_bounds_are_enforced() {
  std::string tooManyReasons = kValidJson;
  const std::string reason =
      "{\"code\":\"disk_high\",\"severity\":\"warning\",\"message\":\"System disk is 82% full\"}";
  replaceOne(tooManyReasons, reason, reason + "," + reason + "," + reason + "," + reason);
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(tooManyReasons).error);

  std::string tooManyServices = kValidJson;
  const std::string service = "{\"id\":\"hub\",\"name\":\"Hub\",\"state\":\"healthy\"}";
  replaceOne(tooManyServices, service,
             service + ",{\"id\":\"a\",\"name\":\"A\",\"state\":\"healthy\"},"
                       "{\"id\":\"b\",\"name\":\"B\",\"state\":\"healthy\"},"
                       "{\"id\":\"c\",\"name\":\"C\",\"state\":\"healthy\"}");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(tooManyServices).error);

  std::string longSummary = kValidJson;
  replaceOne(longSummary, "System disk is 82% full", std::string(65, 'x'));
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(longSummary).error);

  std::string badId = kValidJson;
  replaceOne(badId, "\"id\":\"hub\"", "\"id\":\"Not valid\"");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(badId).error);

  std::string badUtf8 = kValidJson;
  replaceOne(badUtf8, "\"name\":\"Hub\"", std::string("\"name\":\"") + char(0xC3) + "\"");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(badUtf8).error);

  std::string badUnknownUtf8 = kValidJson;
  replaceOne(badUnknownUtf8, "\"services\":[",
             std::string("\"future\":\"") + char(0xC3) + "\",\"services\":[");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(badUnknownUtf8).error);

  std::string embeddedNull = kValidJson;
  embeddedNull.insert(embeddedNull.find("\"services\""), 1, '\0');
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(embeddedNull).error);

  std::string escapedNull = kValidJson;
  replaceOne(escapedNull, "\"name\":\"Hub\"", "\"name\":\"Hu\\u0000b\"");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(escapedNull).error);
}

void test_cross_field_invariants_and_unique_service_ids_are_enforced() {
  std::string healthyReasons = kValidJson;
  replaceOne(healthyReasons, "\"overall\":\"warning\"", "\"overall\":\"healthy\"");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(healthyReasons).error);

  std::string warningNoReasons = kValidJson;
  replaceOne(warningNoReasons,
             "[{\"code\":\"disk_high\",\"severity\":\"warning\",\"message\":\"System disk is 82% full\"}]",
             "[]");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(warningNoReasons).error);

  std::string summaryMismatch = kValidJson;
  replaceOne(summaryMismatch, "\"summary\":\"System disk is 82% full\"",
             "\"summary\":\"Different\"");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(summaryMismatch).error);

  std::string severityMismatch = kValidJson;
  replaceOne(severityMismatch, "\"severity\":\"warning\"",
             "\"severity\":\"critical\"");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(severityMismatch).error);

  std::string duplicateIds = kValidJson;
  replaceOne(duplicateIds, "\"id\":\"backup\"", "\"id\":\"hub\"");
  TEST_ASSERT_EQUAL(nova::PollError::InvalidContract, decode(duplicateIds).error);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_contract_decodes_when_delivered_in_small_chunks);
  RUN_TEST(test_null_metrics_remain_unavailable_and_unknown_fields_are_ignored);
  RUN_TEST(test_http_and_transport_contract_errors_are_classified);
  RUN_TEST(test_body_and_declared_length_are_strictly_bounded);
  RUN_TEST(test_schema_enum_and_numeric_violations_reject_whole_snapshot);
  RUN_TEST(test_missing_wrong_type_and_malformed_fields_reject_snapshot);
  RUN_TEST(test_count_string_identifier_and_utf8_bounds_are_enforced);
  RUN_TEST(test_cross_field_invariants_and_unique_service_ids_are_enforced);
  return UNITY_END();
}
