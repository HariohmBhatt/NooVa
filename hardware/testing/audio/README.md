# HW-005 Audio Test

Standalone validation firmware for the Waveshare ESP32-S3 Touch LCD 3.5 ES8311
codec, I2S bus, microphone, and speaker path.

## Source Of Truth

The I2C address, I2C pins, I2S pins, 44.1 kHz rate, 256x MCLK, and stereo
16-bit format are copied from Waveshare's official
[`04_es8311_example`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5/blob/main/Arduino/examples/04_es8311_example/04_es8311_example.ino):

- ES8311 I2C address: `0x18`
- I2C SDA/SCL: GPIO `8` / `7`
- I2S MCLK/BCLK/LRCLK: GPIO `12` / `13` / `15`
- I2S data out/data in: GPIO `16` / `14`
- Sample rate: `44100 Hz`
- MCLK: `11289600 Hz`
- Sample format: stereo, 16-bit standard I2S

The current PlatformIO Arduino framework exposes the legacy `driver/i2s.h`
interface rather than the newer `ESP_I2S.h` wrapper used by the official
example. The test therefore uses the legacy driver while preserving the
official pin and clock configuration. ES8311 register writes follow Waveshare's
official `es8311` library; no audio recordings are retained.

## Test Procedure

1. Upload the test with a speaker connected at the board's speaker interface.
2. Open the monitor and send `t`. Start at the emitted 10% volume tone; the
   second tone uses 100% volume after the low-volume check. Stop if distortion
   or unsafe loudness occurs, then verify mute and unmute behavior audibly.
3. Send `r`, then speak or play a known tone near the microphone for five
   seconds. The captured PCM is played back and discarded in RAM.
4. Record the peak/RMS values and any audible clipping, silence, or distortion.

## Commands

```sh
pio run -e audio-test
pio run -e audio-test --target upload --upload-port /dev/cu.usbmodem2101
pio device monitor --baud 115200 --port /dev/cu.usbmodem2101
```

Serial commands are `t` for tone/volume/mute, `r` for record/playback, `s` for
summary, and `h` for help. Audio recordings are allocated in PSRAM for the
five-second round trip and freed immediately after playback.

## Files

- `audio_test.cpp`: test firmware
- `REPORT.md`: execution record and exact serial evidence
