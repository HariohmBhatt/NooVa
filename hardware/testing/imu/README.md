# HW-004 IMU Test

Standalone validation firmware for the Waveshare ESP32-S3 Touch LCD 3.5
QMI8658 accelerometer and gyroscope.

## Source Of Truth

The sensor address, I2C pins, no-IRQ configuration, and baseline sensor setup
are copied from Waveshare's official
[`06_qmi8658_getdata`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5/blob/main/Arduino/examples/06_qmi8658_getdata/06_qmi8658_getdata.ino)
example:

- I2C SDA: GPIO 8
- I2C SCL: GPIO 7
- QMI8658 address: `0x6B` (`QMI8658_L_SLAVE_ADDRESS`)
- IRQ: not connected, `-1`
- Accelerometer: 4 g, 1000 Hz, LPF mode 0
- Gyroscope: 64 dps, 896.8 Hz, LPF mode 3

The test uses SensorLib `0.3.1`, matching the official Arduino library version.

## Test Phases

1. Initialize QMI8658 and run accelerometer and gyroscope self-tests.
2. Configure and enable both sensors, then collect five seconds of stationary
   samples. The firmware checks sample count, approximately 1 g acceleration,
   and stationary gyro bias.
3. Use `m` while moving and rotating the board through all three axes. The
   firmware reports raw samples and axis ranges.
4. Use `f` for a guided six-face capture. The final axis/sign mapping requires
   manual review of the reported face means.

## Commands

```sh
pio run -e imu-test
pio run -e imu-test --target upload --upload-port /dev/cu.usbmodem2101
pio device monitor --baud 115200 --port /dev/cu.usbmodem2101
```

Serial commands are `s` for stationary capture, `m` for motion capture, `f` for
the six-face capture, `t` for temperature, and `h` for help. Do not run another
component firmware while this test is active.

## Files

- `imu_test.cpp`: test firmware
- `REPORT.md`: execution record and exact serial evidence
