# HW-003 Wi-Fi Test

Standalone validation firmware for the ESP32-S3 2.4 GHz Wi-Fi radio.

## Credential Handling

The test does not contain credentials. Convert the values to hexadecimal and
inject them only for the local build and upload process through
`WIFI_TEST_BUILD_FLAGS`; do not commit the command, password, generated
binaries, or serial output containing private network data.

## Automated Test Phases

1. Scan nearby networks and verify the target SSID is present on a 2.4 GHz channel.
2. Associate in station mode and print DHCP, DNS, channel, and RSSI details.
3. Resolve `example.com` and complete a small HTTP transaction.
4. Attempt an intentionally unavailable access point while reporting progress.
5. Perform five forced disconnect and reconnect cycles without rebooting.
6. Remain connected and expose an optional SoftAP command for a client check.

## Commands

Replace the placeholders locally. The password is not stored in the repository.
The `xxd` commands produce only hexadecimal build tokens.

```sh
SSID_HEX="$(printf '%s' 'YOUR_SSID' | xxd -p -c 256)"
PASSWORD_HEX="$(printf '%s' 'YOUR_PASSWORD' | xxd -p -c 256)"
WIFI_TEST_BUILD_FLAGS="-DWIFI_TEST_SSID_HEX=$SSID_HEX -DWIFI_TEST_PASSWORD_HEX=$PASSWORD_HEX" \
  pio run -e wifi-test

WIFI_TEST_BUILD_FLAGS="-DWIFI_TEST_SSID_HEX=$SSID_HEX -DWIFI_TEST_PASSWORD_HEX=$PASSWORD_HEX" \
  pio run -e wifi-test --target upload --upload-port /dev/cu.usbmodem2101

pio device monitor --baud 115200 --port /dev/cu.usbmodem2101
```

The monitor commands are `a` to start the optional SoftAP, `x` to stop it,
`s` for a summary, and `h` for help. The optional AP test requires a separate
client to join `NOVA-HW-003-AP` with password `NovaHw003!`, then request
`http://192.168.4.1/`. The firmware returns `NOVA-HW-003-AP-PASS` and records
the payload check when that request arrives.

## Files

- `wifi_test.cpp`: test firmware
- `REPORT.md`: execution record and redacted serial evidence
