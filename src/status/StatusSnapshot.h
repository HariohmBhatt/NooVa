#pragma once

#include <cstddef>
#include <cstdint>

namespace nova {

constexpr size_t kMaxReasons = 3;
constexpr size_t kMaxServices = 4;
constexpr size_t kSummaryCapacity = 65;
constexpr size_t kReasonCodeCapacity = 33;
constexpr size_t kReasonMessageCapacity = 97;
constexpr size_t kServiceIdCapacity = 25;
constexpr size_t kServiceNameCapacity = 25;

/** A nullable scalar without heap allocation or sentinel numeric values. */
template <typename T>
struct Nullable {
  T value{};
  bool available = false;

  static constexpr Nullable withValue(T newValue) {
    return Nullable{newValue, true};
  }
};

enum class ReportedSeverity : uint8_t { Healthy, Warning, Critical };
enum class ServiceState : uint8_t { Healthy, Warning, Critical, Unknown };

struct StatusReason {
  char code[kReasonCodeCapacity]{};
  ReportedSeverity severity = ReportedSeverity::Warning;
  char message[kReasonMessageCapacity]{};
};

struct ServiceStatus {
  char id[kServiceIdCapacity]{};
  char name[kServiceNameCapacity]{};
  ServiceState state = ServiceState::Unknown;
};

/**
 * Complete schema-v1 status record.
 *
 * Every variable-length protocol field has compile-time capacity. Copying this
 * value never allocates, which keeps both model transitions and UI handoff
 * deterministic on the ESP32.
 */
struct StatusSnapshot {
  uint32_t sequence = 0;
  uint64_t generatedAtEpochS = 0;
  ReportedSeverity overall = ReportedSeverity::Healthy;
  char summary[kSummaryCapacity]{};
  StatusReason reasons[kMaxReasons]{};
  uint8_t reasonCount = 0;
  Nullable<uint16_t> cpuPercentTenths{};
  Nullable<uint16_t> memoryPercentTenths{};
  Nullable<uint16_t> diskPercentTenths{};
  Nullable<uint32_t> uptimeSeconds{};
  ServiceStatus services[kMaxServices]{};
  uint8_t serviceCount = 0;
};

enum class PollDisposition : uint8_t { Accepted, TransientError, MonitorError };

enum class PollError : uint8_t {
  None,
  Timeout,
  DnsOrConnect,
  RateLimited,
  ServerFailure,
  Authentication,
  UnsupportedSchema,
  InvalidContract,
  OversizedBody,
  TlsValidation,
  UnexpectedHttpStatus,
};

/** Result emitted by StatusClient and consumed by SentinelModel. */
struct PollOutcome {
  PollDisposition disposition = PollDisposition::TransientError;
  PollError error = PollError::None;
  StatusSnapshot snapshot{};

  static PollOutcome accepted(const StatusSnapshot& value) {
    PollOutcome outcome{};
    outcome.disposition = PollDisposition::Accepted;
    outcome.snapshot = value;
    return outcome;
  }

  static PollOutcome transientError(PollError value) {
    PollOutcome outcome{};
    outcome.disposition = PollDisposition::TransientError;
    outcome.error = value;
    return outcome;
  }

  static PollOutcome monitorError(PollError value) {
    PollOutcome outcome{};
    outcome.disposition = PollDisposition::MonitorError;
    outcome.error = value;
    return outcome;
  }
};

}  // namespace nova
