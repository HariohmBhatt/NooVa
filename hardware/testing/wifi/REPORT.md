# HW-003 Wi-Fi Test Report

## Status

`PASS` (station suite)

Credentials are intentionally excluded from this report. The target network
name and any private network details should be redacted before committing exact
serial captures. The optional SoftAP started successfully, but its client
payload exchange is `BLOCKED` because the available Mac could not switch away
from its active network without disconnecting the user.

## Test Definition

- Test ID: `HW-003`
- Device: Waveshare ESP32-S3 Touch LCD 3.5
- Radio: ESP32-S3 2.4 GHz Wi-Fi
- Test firmware: `hardware/testing/wifi/wifi_test.cpp`
- PlatformIO environment: `wifi-test`
- DNS endpoint: `example.com`
- HTTP endpoint: `example.com:80`
- Reconnect cycles: 5

## Execution Record

- Date: 2026-08-08
- Firmware commit: `7b7cb2e`
- Board serial: `1C:DB:D4:79:7D:AC`
- Board MAC: `1c:db:d4:79:7d:ac`
- Upload port: `/dev/cu.usbmodem2101`
- Target SSID: redacted, operator supplied
- Upload result: `SUCCESS`
- Target channel: `8` (2.4 GHz)
- DHCP address: redacted private address
- Gateway and DNS: redacted private addresses
- RSSI observed: `-66` to `-51` dBm
- Accessories and conditions: USB-Serial/JTAG connection, target access point available

## Acceptance Results

- [x] Target network found in scan on a 2.4 GHz channel
- [x] Station association succeeded
- [x] DHCP address, gateway, subnet, and DNS were reported
- [x] DNS resolution succeeded
- [x] HTTP transaction succeeded with `HTTP/1.1 200 OK`
- [x] RSSI and channel were reported while associated
- [x] Unavailable AP was rejected without blocking the test loop
- [x] Five disconnect and reconnect cycles recovered without reboot
- [x] Optional SoftAP started at `192.168.4.1`
- [ ] Optional SoftAP client connected and exchanged the known payload (blocked)

## Exact Serial Output

```text
[HW-003] SCAN_COUNT=6
[HW-003] NETWORK INDEX=1 SSID=<target-redacted> CHANNEL=8 RSSI=-57 ENCRYPTION=3
[HW-003][PASS] TARGET_NETWORK_FOUND
[HW-003][PASS] TARGET_NETWORK_2G_CHANNEL
[HW-003][PASS] STATION_CONNECTED
[HW-003] IP=<private> SUBNET=255.255.255.0 GATEWAY=<private> DNS=<private> CHANNEL=8 RSSI=-56
[HW-003] DNS_RESULT=1 HOST=example.com ADDRESS=<public-address>
[HW-003][PASS] DNS_RESOLUTION
[HW-003] HTTP_RESPONSE=HTTP/1.1 200 OK
[HW-003][PASS] HTTP_TRANSACTION
[HW-003][PASS] UNAVAILABLE_AP_REJECTED
[HW-003][PASS] RECONNECT_CYCLE=1 PHASE=DISCONNECTED
[HW-003][PASS] RECONNECT_CYCLE=1 PHASE=RECONNECTED
[HW-003][PASS] RECONNECT_CYCLE=2 PHASE=DISCONNECTED
[HW-003][PASS] RECONNECT_CYCLE=2 PHASE=RECONNECTED
[HW-003][PASS] RECONNECT_CYCLE=3 PHASE=DISCONNECTED
[HW-003][PASS] RECONNECT_CYCLE=3 PHASE=RECONNECTED
[HW-003][PASS] RECONNECT_CYCLE=4 PHASE=DISCONNECTED
[HW-003][PASS] RECONNECT_CYCLE=4 PHASE=RECONNECTED
[HW-003][PASS] RECONNECT_CYCLE=5 PHASE=DISCONNECTED
[HW-003][PASS] RECONNECT_CYCLE=5 PHASE=RECONNECTED
[HW-003] AUTOMATED_CHECKS=PASS
[HW-003][PASS] OPTIONAL_SOFTAP_STARTED
[HW-003] SOFTAP_SSID=NOVA-HW-003-AP IP=192.168.4.1 CLIENTS=0
```

## Observations

The station connection remained active at a private DHCP address during the test and
the radio recovered the same lease after each intentional disconnect. The
`AUTH_EXPIRE`, `ASSOC_LEAVE`, and `NO_AP_FOUND` warnings corresponded to the
intentional disconnect and unavailable-AP phases. The optional SoftAP client
payload could not be exercised without a second client interface.

## Defects and Follow-up

- The optional SoftAP client payload check requires a separate client device.
