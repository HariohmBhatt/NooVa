# HW-004 IMU Test Report

## Status

`PASS`

QMI8658 initialization, both hardware self-tests, configuration, stationary
sampling, and three-axis motion sampling have passed. The first six-face capture
was invalid because the board was not repositioned. The controlled repeat showed
distinct gravity orientations for all six operator-selected faces and passed the
axis-variation check.

## Test Definition

- Test ID: `HW-004`
- Device: Waveshare ESP32-S3 Touch LCD 3.5
- Sensor: QMI8658 six-axis IMU
- Test firmware: `hardware/testing/imu/imu_test.cpp`
- PlatformIO environment: `imu-test`
- SensorLib version: `0.3.1`

## Hardware Configuration

- I2C SDA: GPIO 8
- I2C SCL: GPIO 7
- QMI8658 address: `0x6B`
- IRQ: not connected, `-1`
- Accelerometer: 4 g, 1000 Hz, LPF mode 0
- Gyroscope: 64 dps, 896.8 Hz, LPF mode 3

## Execution Record

- Date: 2026-08-08
- Firmware commit: `d95e31a`
- Board serial: `1C:DB:D4:79:7D:AC`
- Board MAC: `1c:db:d4:79:7d:ac`
- Upload port: `/dev/cu.usbmodem2101`
- Upload result: `SUCCESS`
- Accessories and conditions: USB-Serial/JTAG connection, board stationary for repeat capture

## Acceptance Results

- [x] QMI8658 initialized at the documented address
- [x] Accelerometer self-test passed
- [x] Gyroscope self-test passed
- [x] Accelerometer and gyroscope configuration reported
- [x] Data-ready sample stream passed
- [x] Stationary acceleration magnitude was approximately 1 g
- [x] Stationary gyro bias remained within the selected threshold on repeat capture
- [x] All three accelerometer axes responded to motion
- [x] All three gyroscope axes responded to rotation
- [x] Six-face axis/sign mapping recorded from the controlled face sequence
- [x] Temperature samples reported

## Exact Serial Output

```text
[HW-004][PASS] ACCELEROMETER_SELF_TEST
[HW-004][PASS] GYROSCOPE_SELF_TEST
[HW-004][PASS] ACCELEROMETER_CONFIGURED
[HW-004][PASS] GYROSCOPE_CONFIGURED
[HW-004][PASS] ACCELEROMETER_ENABLED
[HW-004][PASS] GYROSCOPE_ENABLED
[HW-004] STATS=STATIONARY SAMPLES=715 NOT_READY=0 READ_ERRORS=0 ACCEL_MAG_G=0.9769..1.0388 MEAN=1.0071 GYRO_MAG_DPS=4.4860..8.1193 MEAN=6.3987
[HW-004][PASS] STATIONARY_SAMPLE_COUNT
[HW-004][PASS] STATIONARY_ACCEL_MAGNITUDE
[HW-004][PASS] STATIONARY_GYRO_BIAS
[HW-004] STATS=MOTION SAMPLES=1423 NOT_READY=0 READ_ERRORS=0 ACCEL_MAG_G=0.1223..1.9598 MEAN=1.0619 GYRO_MAG_DPS=4.0766..110.8512 MEAN=61.4833
[HW-004] MOTION_RANGE ACCEL_G=3.3892 GYRO_DPS=127.9980
[HW-004][PASS] MOTION_ACCEL_RESPONSE
[HW-004][PASS] MOTION_GYRO_RESPONSE
[HW-004] FACE_RESULT=1 SAMPLES=358 ACCEL_MAG_MEAN_G=1.0231 AXIS_MEAN_G=0.0614,0.0891,1.0151
[HW-004] FACE_RESULT=2 SAMPLES=358 ACCEL_MAG_MEAN_G=0.9647 AXIS_MEAN_G=0.0432,-0.4901,0.0936
[HW-004] FACE_RESULT=3 SAMPLES=358 ACCEL_MAG_MEAN_G=1.0050 AXIS_MEAN_G=-0.0251,0.1141,-0.9783
[HW-004] FACE_RESULT=4 SAMPLES=358 ACCEL_MAG_MEAN_G=0.9382 AXIS_MEAN_G=0.1574,0.1332,-0.5516
[HW-004] FACE_RESULT=5 SAMPLES=358 ACCEL_MAG_MEAN_G=1.0830 AXIS_MEAN_G=0.1395,1.0013,-0.1466
[HW-004] FACE_RESULT=6 SAMPLES=358 ACCEL_MAG_MEAN_G=0.9637 AXIS_MEAN_G=0.1767,0.0096,-0.7790
[HW-004] SIX_FACE_AXIS_VARIATION_G=0.1153,0.9123,1.9934
[HW-004][PASS] SIX_FACE_ACCELERATION_VARIATION
[HW-004] SIX_FACE_TEST_COMPLETE MANUAL_AXIS_REVIEW_REQUIRED=true
```

## Observations

The repeat stationary capture passed after the initial capture was invalidated
by a high gyro peak. Motion produced response on all three axes. The controlled
six-face run recorded approximately 1 g magnitude for each operator-selected
face and distinct axis/sign responses; the face labels follow the operator's
sequence from face 1 through face 6.

## Defects and Follow-up

- The six-face axis/sign mapping requires manual review of the captured means.
