#include "StatusClient.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "StatusCodec.h"

namespace nova {
namespace {

bool copyConfigString(const char* source, char* destination, size_t capacity) {
  if (source == nullptr) {
    return false;
  }
  const size_t length = std::strlen(source);
  if (length == 0 || length >= capacity) {
    return false;
  }
  std::memcpy(destination, source, length + 1);
  return true;
}

const char* skipSpaces(const char* value) {
  while (*value == ' ' || *value == '\t') {
    ++value;
  }
  return value;
}

bool parseUnsigned(const char* value, unsigned long& parsed) {
  value = skipSpaces(value);
  errno = 0;
  char* end = nullptr;
  const unsigned long result = std::strtoul(value, &end, 10);
  while (end != nullptr && (*end == ' ' || *end == '\t')) {
    ++end;
  }
  if (errno != 0 || end == value || end == nullptr || *end != '\0' ||
      *value == '-') {
    return false;
  }
  parsed = result;
  return true;
}

bool parseCappedRetryAfter(const char* value, uint32_t& milliseconds) {
  value = skipSpaces(value);
  if (*value < '0' || *value > '9') {
    return false;
  }
  uint32_t seconds = 0;
  while (*value >= '0' && *value <= '9') {
    if (seconds < 60) {
      const uint32_t digit = static_cast<uint32_t>(*value - '0');
      seconds = std::min(60U, seconds * 10U + digit);
    }
    ++value;
  }
  value = skipSpaces(value);
  if (*value != '\0') {
    return false;
  }
  milliseconds = seconds * 1000U;
  return true;
}

PollOutcome classifyHttpStatus(int statusCode) {
  if (statusCode == 401 || statusCode == 403) {
    return PollOutcome::monitorError(PollError::Authentication);
  }
  if (statusCode == 426) {
    return PollOutcome::monitorError(PollError::UnsupportedSchema);
  }
  if (statusCode == 429) {
    return PollOutcome::transientError(PollError::RateLimited);
  }
  if (statusCode >= 500 && statusCode <= 599) {
    return PollOutcome::transientError(PollError::ServerFailure);
  }
  return PollOutcome::monitorError(PollError::UnexpectedHttpStatus);
}

}  // namespace

bool StatusClient::begin(const StatusClientConfig& config) {
  configured_ =
      copyConfigString(config.tlsServerName, tlsServerName_,
                       sizeof(tlsServerName_)) &&
      copyConfigString(config.path, path_, sizeof(path_)) && path_[0] == '/' &&
      copyConfigString(config.bearerToken, token_, sizeof(token_));
  return configured_;
}

bool StatusClient::configured() const { return configured_; }

bool StatusClient::update(uint32_t nowMs, bool wifiConnected,
                          StatusTransport& transport, PollOutcome& outcome) {
  if (!configured_) {
    return false;
  }

  if (!wifiConnected) {
    if (wifiWasConnected_ || inFlight_) {
      transport.cancel();
    }
    wifiWasConnected_ = false;
    inFlight_ = false;
    waiting_ = false;
    responseLength_ = 0;
    return false;
  }

  if (!wifiWasConnected_) {
    wifiWasConnected_ = true;
    waiting_ = false;
  }

  if (inFlight_) {
    HttpsTransportEvent event{};
    if (!transport.take(event)) {
      return false;
    }
    if (event.type == HttpsTransportEventType::ResponseBytes) {
      if (event.bytes == nullptr || event.length == 0 ||
          event.length > kMaxHttpsResponseChunkBytes) {
        transport.cancel();
        inFlight_ = false;
        return complete(PollOutcome::monitorError(PollError::InvalidContract),
                        kNoRetryAfter, nowMs, outcome);
      }
      if (event.length > kMaxRawHttpResponseBytes - responseLength_) {
        transport.cancel();
        inFlight_ = false;
        return complete(PollOutcome::monitorError(PollError::OversizedBody),
                        kNoRetryAfter, nowMs, outcome);
      }
      std::memcpy(responseBytes_ + responseLength_, event.bytes, event.length);
      responseLength_ += event.length;
      return false;
    }

    inFlight_ = false;
    if (event.type == HttpsTransportEventType::ResponseComplete) {
      uint32_t retryAfterMs = kNoRetryAfter;
      const PollOutcome result = processResponse(retryAfterMs);
      return complete(result, retryAfterMs, nowMs, outcome);
    }
    if (event.type == HttpsTransportEventType::Timeout) {
      return complete(PollOutcome::transientError(PollError::Timeout),
                      kNoRetryAfter, nowMs, outcome);
    }
    if (event.type == HttpsTransportEventType::ConnectFailure) {
      return complete(PollOutcome::transientError(PollError::DnsOrConnect),
                      kNoRetryAfter, nowMs, outcome);
    }
    if (event.type == HttpsTransportEventType::TlsValidationFailure) {
      return complete(PollOutcome::monitorError(PollError::TlsValidation),
                      kNoRetryAfter, nowMs, outcome);
    }
    return complete(PollOutcome::monitorError(PollError::OversizedBody),
                    kNoRetryAfter, nowMs, outcome);
  }

  if (waiting_ && nowMs - completedAtMs_ < waitDurationMs_) {
    return false;
  }

  HttpsRequest request{};
  if (!buildRequest(request) || !transport.submit(request)) {
    return false;
  }
  inFlight_ = true;
  waiting_ = false;
  responseLength_ = 0;
  return false;
}

bool StatusClient::buildRequest(HttpsRequest& request) const {
  const int length = std::snprintf(
      request.bytes, sizeof(request.bytes),
      "GET %s HTTP/1.1\r\nHost: %s\r\nAccept: application/json\r\n"
      "Authorization: Bearer %s\r\nX-Nova-Schema: 1\r\n"
      "Connection: close\r\n\r\n",
      path_, tlsServerName_, token_);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(request.bytes)) {
    request = {};
    return false;
  }
  request.length = static_cast<uint16_t>(length);
  return true;
}

PollOutcome StatusClient::processResponse(uint32_t& retryAfterMs) const {
  retryAfterMs = kNoRetryAfter;
  if (responseLength_ == 0) {
    return PollOutcome::monitorError(PollError::InvalidContract);
  }

  size_t headerEnd = 0;
  for (size_t index = 0; index + 3 < responseLength_; ++index) {
    if (responseBytes_[index] == '\r' && responseBytes_[index + 1] == '\n' &&
        responseBytes_[index + 2] == '\r' && responseBytes_[index + 3] == '\n') {
      headerEnd = index + 4;
      break;
    }
  }
  if (headerEnd == 0 || headerEnd > kMaxResponseHeaderBytes) {
    return PollOutcome::monitorError(PollError::InvalidContract);
  }

  size_t cursor = 0;
  auto nextLine = [&](char (&line)[kMaxHeaderLineBytes + 1]) -> bool {
    size_t end = cursor;
    while (end + 1 < headerEnd &&
           !(responseBytes_[end] == '\r' && responseBytes_[end + 1] == '\n')) {
      ++end;
    }
    const size_t length = end - cursor;
    if (end + 1 >= headerEnd || length > kMaxHeaderLineBytes) {
      return false;
    }
    std::memcpy(line, responseBytes_ + cursor, length);
    line[length] = '\0';
    cursor = end + 2;
    return true;
  };

  char line[kMaxHeaderLineBytes + 1]{};
  if (!nextLine(line)) {
    return PollOutcome::monitorError(PollError::InvalidContract);
  }
  char protocol[16]{};
  int statusCode = 0;
  if (std::sscanf(line, "%15s %d", protocol, &statusCode) != 2 ||
      std::strncmp(protocol, "HTTP/1.", 7) != 0 || statusCode < 100 ||
      statusCode > 599) {
    return PollOutcome::monitorError(PollError::InvalidContract);
  }

  int32_t contentLength = -1;
  char contentType[40]{};
  bool contentLengthSeen = false;
  bool contentTypeSeen = false;
  while (cursor < headerEnd - 2) {
    if (!nextLine(line)) {
      return PollOutcome::monitorError(PollError::InvalidContract);
    }
    if (line[0] == '\0') {
      break;
    }
    constexpr char kLengthHeader[] = "Content-Length:";
    constexpr char kTypeHeader[] = "Content-Type:";
    constexpr char kTransferHeader[] = "Transfer-Encoding:";
    constexpr char kRetryHeader[] = "Retry-After:";
    if (strncasecmp(line, kLengthHeader, sizeof(kLengthHeader) - 1) == 0) {
      unsigned long parsed = 0;
      if (contentLengthSeen ||
          !parseUnsigned(line + sizeof(kLengthHeader) - 1, parsed) ||
          parsed > static_cast<unsigned long>(INT32_MAX)) {
        return PollOutcome::monitorError(PollError::InvalidContract);
      }
      contentLength = static_cast<int32_t>(parsed);
      contentLengthSeen = true;
    } else if (strncasecmp(line, kTypeHeader, sizeof(kTypeHeader) - 1) == 0) {
      if (contentTypeSeen) {
        return PollOutcome::monitorError(PollError::InvalidContract);
      }
      const char* value = skipSpaces(line + sizeof(kTypeHeader) - 1);
      if (std::strlen(value) >= sizeof(contentType)) {
        return PollOutcome::monitorError(PollError::InvalidContract);
      }
      std::strcpy(contentType, value);
      contentTypeSeen = true;
    } else if (strncasecmp(line, kTransferHeader,
                           sizeof(kTransferHeader) - 1) == 0) {
      return PollOutcome::monitorError(PollError::InvalidContract);
    } else if (strncasecmp(line, kRetryHeader, sizeof(kRetryHeader) - 1) == 0) {
      parseCappedRetryAfter(line + sizeof(kRetryHeader) - 1, retryAfterMs);
    }
  }

  if (statusCode != 200) {
    return classifyHttpStatus(statusCode);
  }
  const size_t bodyLength = responseLength_ - headerEnd;
  if (!contentLengthSeen || !contentTypeSeen || contentLength < 0 ||
      contentLength > 4096 || std::strcmp(contentType, "application/json") != 0 ||
      bodyLength != static_cast<size_t>(contentLength)) {
    return PollOutcome::monitorError(contentLength > 4096
                                         ? PollError::OversizedBody
                                         : PollError::InvalidContract);
  }

  StatusSnapshot snapshot{};
  const PollError decodeError =
      detail::decodeStatusBody(responseBytes_ + headerEnd, bodyLength, snapshot);
  return decodeError == PollError::None ? PollOutcome::accepted(snapshot)
                                        : PollOutcome::monitorError(decodeError);
}

bool StatusClient::complete(PollOutcome result, uint32_t retryAfterMs,
                            uint32_t nowMs, PollOutcome& outcome) {
  completedAtMs_ = nowMs;
  waitDurationMs_ = result.error == PollError::RateLimited &&
                            retryAfterMs != kNoRetryAfter
                        ? retryAfterMs
                        : kPollIntervalMs;
  waiting_ = true;
  responseLength_ = 0;
  outcome = result;
  return true;
}

}  // namespace nova
