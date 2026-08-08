# Read-only board diagnostics

`board-diagnostics` verifies the fixed board identity, 16 MB flash, 8 MB
PSRAM, a bounded 64 KiB PSRAM write/read pattern, and all expected devices on
the shared I2C bus. It then takes two read-only PCF85063 time samples and reads
the AXP2101 identity, status, ADC state, and available telemetry.

The target never changes an RTC register, power rail, charger setting,
shutdown setting, or eFuse. It intentionally does not call the bundled
`SensorPCF85063::begin()` or `XPowersPMU::begin()` because those initializers
write device state. Register-address selection followed by I2C reads is the
only transaction used for RTC and PMIC validation.

## Official configuration

The implementation is pinned to Waveshare's official repository commit
[`283ec84c566c096f8c30493b93dcd4b0bb608de7`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5/tree/283ec84c566c096f8c30493b93dcd4b0bb608de7):

- [AXP2101 example](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5/blob/283ec84c566c096f8c30493b93dcd4b0bb608de7/Arduino/examples/02_axp2101_example/02_axp2101_example.ino)
- [PCF85063 example](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5/blob/283ec84c566c096f8c30493b93dcd4b0bb608de7/Arduino/examples/05_pcf85063_time/05_pcf85063_time.ino)
- [QMI8658 example](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5/blob/283ec84c566c096f8c30493b93dcd4b0bb608de7/Arduino/examples/06_qmi8658_getdata/06_qmi8658_getdata.ino)
- [display/I2C-expander example](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5/blob/283ec84c566c096f8c30493b93dcd4b0bb608de7/Arduino/examples/08_gfx_helloworld/08_gfx_helloworld.ino)
- [touch example](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5/blob/283ec84c566c096f8c30493b93dcd4b0bb608de7/Arduino/examples/11_lvgl_arduino_v8/11_lvgl_arduino_v8.ino)

## Run

```sh
python3 -m unittest hardware/testing/board/test_board_diagnostics_policy.py
pio run -e board-diagnostics
pio run -e board-diagnostics --target upload --upload-port <device-port>
pio device monitor --baud 115200 --port <device-port>
```

Uploading is an explicit operator step. A passing build alone is not hardware
evidence; save the complete serial report from the physical board.
