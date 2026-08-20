#include "StatusCodec.h"

#include <ArduinoJson.h>

#include <cstring>

namespace nova {
namespace {

bool validUtf8(const char* value, size_t length) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(value);
  size_t position = 0;
  while (position < length) {
    if (bytes[position] == 0) {
      return false;  // JSON text cannot contain an unescaped NUL byte.
    }
    if (bytes[position] <= 0x7F) {
      ++position;
      continue;
    }

    uint32_t codepoint = 0;
    uint8_t continuationCount = 0;
    if ((bytes[position] & 0xE0) == 0xC0) {
      codepoint = bytes[position] & 0x1F;
      continuationCount = 1;
      if (codepoint < 2) {  // Reject overlong two-byte encodings.
        return false;
      }
    } else if ((bytes[position] & 0xF0) == 0xE0) {
      codepoint = bytes[position] & 0x0F;
      continuationCount = 2;
    } else if ((bytes[position] & 0xF8) == 0xF0) {
      codepoint = bytes[position] & 0x07;
      continuationCount = 3;
    } else {
      return false;
    }
    ++position;
    for (uint8_t index = 0; index < continuationCount; ++index) {
      if (position >= length || (bytes[position] & 0xC0) != 0x80) {
        return false;
      }
      codepoint = (codepoint << 6) | (bytes[position] & 0x3F);
      ++position;
    }
    if ((continuationCount == 2 && codepoint < 0x800) ||
        (continuationCount == 3 && codepoint < 0x10000) ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF) {
      return false;
    }
  }
  return true;
}

template <size_t Capacity>
bool copyBoundedString(JsonVariantConst source, char (&destination)[Capacity]) {
  if (!source.is<const char*>()) {
    return false;
  }
  const JsonString string = source.as<JsonString>();
  const char* value = string.c_str();
  const size_t length = string.size();
  if (length == 0 || length >= Capacity || !validUtf8(value, length)) {
    return false;
  }
  std::memcpy(destination, value, length + 1);
  return true;
}

bool validIdentifier(const char* value) {
  for (size_t index = 0; value[index] != '\0'; ++index) {
    const char character = value[index];
    if (!((character >= 'a' && character <= 'z') ||
          (index > 0 && character >= '0' && character <= '9') ||
          (index > 0 && character == '_'))) {
      return false;
    }
  }
  return true;
}

bool parseSeverity(JsonVariantConst source, ReportedSeverity& severity) {
  if (!source.is<const char*>()) {
    return false;
  }
  const char* value = source.as<const char*>();
  if (std::strcmp(value, "healthy") == 0) {
    severity = ReportedSeverity::Healthy;
  } else if (std::strcmp(value, "warning") == 0) {
    severity = ReportedSeverity::Warning;
  } else if (std::strcmp(value, "critical") == 0) {
    severity = ReportedSeverity::Critical;
  } else {
    return false;
  }
  return true;
}

bool parseReasonSeverity(JsonVariantConst source, ReportedSeverity& severity) {
  return parseSeverity(source, severity) && severity != ReportedSeverity::Healthy;
}

bool parseServiceState(JsonVariantConst source, ServiceState& state) {
  if (!source.is<const char*>()) {
    return false;
  }
  const char* value = source.as<const char*>();
  if (std::strcmp(value, "healthy") == 0) {
    state = ServiceState::Healthy;
  } else if (std::strcmp(value, "warning") == 0) {
    state = ServiceState::Warning;
  } else if (std::strcmp(value, "critical") == 0) {
    state = ServiceState::Critical;
  } else if (std::strcmp(value, "unknown") == 0) {
    state = ServiceState::Unknown;
  } else {
    return false;
  }
  return true;
}

template <typename T>
bool parseNullableUnsigned(JsonObjectConst object, const char* key, T maximum,
                           Nullable<T>& result) {
  if (!object.containsKey(key)) {
    return false;
  }
  const JsonVariantConst value = object[key];
  if (value.isNull()) {
    result = {};
    return true;
  }
  if (!value.is<T>()) {
    return false;
  }
  const T parsed = value.as<T>();
  if (parsed > maximum) {
    return false;
  }
  result = Nullable<T>::withValue(parsed);
  return true;
}

void configureFilter(StaticJsonDocument<1536>& filter) {
  filter["schema_version"] = true;
  filter["sequence"] = true;
  filter["generated_at_epoch_s"] = true;
  filter["overall"] = true;
  filter["summary"] = true;
  filter["reasons"][0]["code"] = true;
  filter["reasons"][0]["severity"] = true;
  filter["reasons"][0]["message"] = true;
  filter["metrics"]["cpu_percent_tenths"] = true;
  filter["metrics"]["memory_percent_tenths"] = true;
  filter["metrics"]["disk_percent_tenths"] = true;
  filter["metrics"]["uptime_seconds"] = true;
  filter["services"][0]["id"] = true;
  filter["services"][0]["name"] = true;
  filter["services"][0]["state"] = true;
}

}  // namespace

StatusDecoder::StatusDecoder(const HttpResponseMetadata& metadata)
    : metadata_(metadata) {
  reset(metadata);
}

void StatusDecoder::reset(const HttpResponseMetadata& metadata) {
  metadata_ = metadata;
  metadata_.contentType[sizeof(metadata_.contentType) - 1] = '\0';
  received_ = 0;
  framingError_ = PollError::None;
  if (metadata_.statusCode == 200) {
    if (metadata_.contentLength < 0) {
      framingError_ = PollError::InvalidContract;
    } else if (metadata_.contentLength >
               static_cast<int32_t>(kMaxStatusBodyBytes)) {
      framingError_ = PollError::OversizedBody;
    } else if (std::strcmp(metadata_.contentType, "application/json") != 0) {
      framingError_ = PollError::InvalidContract;
    }
  }
}

bool StatusDecoder::append(const uint8_t* data, size_t length) {
  if (data == nullptr || framingError_ != PollError::None) {
    return length == 0;
  }
  if (length > kMaxStatusBodyBytes - received_) {
    framingError_ = PollError::OversizedBody;
    return false;
  }
  std::memcpy(body_ + received_, data, length);
  received_ += length;
  return true;
}

PollOutcome StatusDecoder::finish() {
  if (metadata_.statusCode != 200) {
    return classifyHttpError();
  }
  if (framingError_ != PollError::None) {
    return PollOutcome::monitorError(framingError_);
  }
  if (received_ != static_cast<size_t>(metadata_.contentLength)) {
    return PollOutcome::monitorError(PollError::InvalidContract);
  }
  body_[received_] = '\0';

  StatusSnapshot snapshot{};
  if (!decodeSnapshot(snapshot)) {
    return PollOutcome::monitorError(PollError::InvalidContract);
  }
  return PollOutcome::accepted(snapshot);
}

PollOutcome StatusDecoder::classifyHttpError() const {
  if (metadata_.statusCode == 401 || metadata_.statusCode == 403) {
    return PollOutcome::monitorError(PollError::Authentication);
  }
  if (metadata_.statusCode == 426) {
    return PollOutcome::monitorError(PollError::UnsupportedSchema);
  }
  if (metadata_.statusCode == 429) {
    return PollOutcome::transientError(PollError::RateLimited);
  }
  if (metadata_.statusCode >= 500 && metadata_.statusCode <= 599) {
    return PollOutcome::transientError(PollError::ServerFailure);
  }
  return PollOutcome::monitorError(PollError::UnexpectedHttpStatus);
}

bool StatusDecoder::decodeSnapshot(StatusSnapshot& snapshot) {
  // Validate the entire body before filtering unknown JSON fields so malformed
  // UTF-8 can never hide inside an extension the current schema ignores.
  if (!validUtf8(body_, received_)) {
    return false;
  }
  // These fixed workspaces live in static storage because parsing happens on
  // Arduino's relatively small loop-task stack. StatusClient permits only one
  // in-flight response, so the non-reentrant workspace is an intentional bound.
  static StaticJsonDocument<1536> filter;
  static StaticJsonDocument<4608> document;
  filter.clear();
  document.clear();
  configureFilter(filter);
  const DeserializationError error = deserializeJson(
      document, body_, DeserializationOption::Filter(filter));
  if (error || !document.is<JsonObject>()) {
    return false;
  }
  const JsonObjectConst root = document.as<JsonObjectConst>();
  if (!root["schema_version"].is<uint8_t>() ||
      root["schema_version"].as<uint8_t>() != 1 ||
      !root["sequence"].is<uint32_t>() ||
      !root["generated_at_epoch_s"].is<uint64_t>() ||
      !parseSeverity(root["overall"], snapshot.overall) ||
      !copyBoundedString(root["summary"], snapshot.summary)) {
    return false;
  }
  snapshot.sequence = root["sequence"].as<uint32_t>();
  snapshot.generatedAtEpochS = root["generated_at_epoch_s"].as<uint64_t>();

  if (!root["reasons"].is<JsonArrayConst>() ||
      !root["metrics"].is<JsonObjectConst>() ||
      !root["services"].is<JsonArrayConst>()) {
    return false;
  }

  const JsonArrayConst reasons = root["reasons"].as<JsonArrayConst>();
  if (reasons.size() > kMaxReasons) {
    return false;
  }
  for (JsonVariantConst item : reasons) {
    if (!item.is<JsonObjectConst>()) {
      return false;
    }
    StatusReason& reason = snapshot.reasons[snapshot.reasonCount];
    const JsonObjectConst object = item.as<JsonObjectConst>();
    if (!copyBoundedString(object["code"], reason.code) ||
        !validIdentifier(reason.code) ||
        !parseReasonSeverity(object["severity"], reason.severity) ||
        !copyBoundedString(object["message"], reason.message)) {
      return false;
    }
    ++snapshot.reasonCount;
  }

  const JsonObjectConst metrics = root["metrics"].as<JsonObjectConst>();
  if (!parseNullableUnsigned<uint16_t>(metrics, "cpu_percent_tenths", 1000,
                                       snapshot.cpuPercentTenths) ||
      !parseNullableUnsigned<uint16_t>(metrics, "memory_percent_tenths", 1000,
                                       snapshot.memoryPercentTenths) ||
      !parseNullableUnsigned<uint16_t>(metrics, "disk_percent_tenths", 1000,
                                       snapshot.diskPercentTenths) ||
      !parseNullableUnsigned<uint32_t>(metrics, "uptime_seconds", UINT32_MAX,
                                       snapshot.uptimeSeconds)) {
    return false;
  }

  const JsonArrayConst services = root["services"].as<JsonArrayConst>();
  if (services.size() > kMaxServices) {
    return false;
  }
  for (JsonVariantConst item : services) {
    if (!item.is<JsonObjectConst>()) {
      return false;
    }
    ServiceStatus& service = snapshot.services[snapshot.serviceCount];
    const JsonObjectConst object = item.as<JsonObjectConst>();
    if (!copyBoundedString(object["id"], service.id) ||
        !validIdentifier(service.id) ||
        !copyBoundedString(object["name"], service.name) ||
        !parseServiceState(object["state"], service.state)) {
      return false;
    }
    for (uint8_t prior = 0; prior < snapshot.serviceCount; ++prior) {
      if (std::strcmp(snapshot.services[prior].id, service.id) == 0) {
        return false;
      }
    }
    ++snapshot.serviceCount;
  }

  if (snapshot.overall == ReportedSeverity::Healthy) {
    return snapshot.reasonCount == 0 &&
           std::strcmp(snapshot.summary, "All monitored systems normal") == 0;
  }
  return snapshot.reasonCount > 0 &&
         snapshot.reasons[0].severity == snapshot.overall &&
         std::strcmp(snapshot.summary, snapshot.reasons[0].message) == 0;
}

}  // namespace nova
