#pragma once

#include <cstdint>

// Copy to Provisioning.h and fill locally. Provisioning.h is ignored by git.
// The TLS value must contain the CA that issued the server certificate; there
// is deliberately no insecure or certificate-bypass option.
namespace nova::provisioning {

constexpr char kWifiSsid[] = "YOUR_2_4_GHZ_SSID";
constexpr char kWifiPassword[] = "YOUR_WIFI_PASSWORD";
constexpr char kStatusAddress[] = "192.168.29.225";
constexpr char kStatusTlsName[] = "nova-sentinel.local";
constexpr uint16_t kStatusPort = 8443;
constexpr char kStatusPath[] = "/v1/status";
constexpr char kDeviceToken[] = "YOUR_RANDOM_DEVICE_TOKEN";
constexpr char kTlsCaPem[] = R"pem(-----BEGIN CERTIFICATE-----
YOUR_PRIVATE_CA_CERTIFICATE
-----END CERTIFICATE-----
)pem";

}  // namespace nova::provisioning
