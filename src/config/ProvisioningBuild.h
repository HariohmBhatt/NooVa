#pragma once

#include <cstdint>

// Build flags offer a CI/development alternative to an ignored Provisioning.h.
// String values must be quoted by the build environment; no defaults contain
// operational credentials or weaken TLS.
#ifndef NOVA_WIFI_SSID
#define NOVA_WIFI_SSID ""
#endif
#ifndef NOVA_WIFI_PASSWORD
#define NOVA_WIFI_PASSWORD ""
#endif
#ifndef NOVA_STATUS_ADDRESS
#define NOVA_STATUS_ADDRESS ""
#endif
#ifndef NOVA_STATUS_TLS_NAME
#define NOVA_STATUS_TLS_NAME ""
#endif
#ifndef NOVA_STATUS_PORT
#define NOVA_STATUS_PORT 8443
#endif
#ifndef NOVA_STATUS_PATH
#define NOVA_STATUS_PATH "/v1/status"
#endif
#ifndef NOVA_DEVICE_TOKEN
#define NOVA_DEVICE_TOKEN ""
#endif
#ifndef NOVA_TLS_CA_PEM
#define NOVA_TLS_CA_PEM ""
#endif

namespace nova::provisioning {

constexpr char kWifiSsid[] = NOVA_WIFI_SSID;
constexpr char kWifiPassword[] = NOVA_WIFI_PASSWORD;
constexpr char kStatusAddress[] = NOVA_STATUS_ADDRESS;
constexpr char kStatusTlsName[] = NOVA_STATUS_TLS_NAME;
constexpr uint16_t kStatusPort = NOVA_STATUS_PORT;
constexpr char kStatusPath[] = NOVA_STATUS_PATH;
constexpr char kDeviceToken[] = NOVA_DEVICE_TOKEN;
constexpr char kTlsCaPem[] = NOVA_TLS_CA_PEM;

}  // namespace nova::provisioning
