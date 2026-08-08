# HW-005 Audio Test Report

## Status

`IN PROGRESS`

The ES8311 identity registers, codec initialization, I2S initialization, tone
generation, volume/mute register controls, and recorded-buffer playback path
passed. The first five-second microphone capture returned only a near-silent
signal (`PEAK=36`, `RMS=5.04`), so microphone input acceptance and audible
operator review remain pending.

## Test Definition

- Test ID: `HW-005`
- Device: Waveshare ESP32-S3 Touch LCD 3.5
- Codec: ES8311
- Test firmware: `hardware/testing/audio/audio_test.cpp`
- PlatformIO environment: `audio-test`
- Sample rate: `44100 Hz`
- Format: stereo, 16-bit standard I2S
- Capture duration: 5 seconds

## Hardware Configuration

- ES8311 I2C address: `0x18`
- I2C SDA/SCL: GPIO `8` / `7`
- I2S MCLK/BCLK/LRCLK: GPIO `12` / `13` / `15`
- I2S data out/data in: GPIO `16` / `14`
- MCLK: `11289600 Hz`

## Execution Record

- Date: 2026-08-08
- Firmware commit: `a11fe91`
- Board serial: `1C:DB:D4:79:7D:AC`
- Board MAC: `1c:db:d4:79:7d:ac`
- Upload port: `/dev/cu.usbmodem2101`
- Upload result: `SUCCESS`
- Speaker/microphone accessories: pending operator confirmation
- Volume conditions: starts at 10%, maximum test level 20%

## Acceptance Results

- [x] ES8311 responded at the documented address
- [x] Codec identity registers were readable
- [x] I2S initialized with the verified pin and clock configuration
- [ ] Low-volume tone output was audible and clean
- [x] Volume register control completed
- [x] Mute and unmute register control completed
- [ ] Microphone capture returned non-zero unsaturated input
- [x] Five-second capture completed without I2S read failure
- [x] Recorded audio playback completed
- [ ] Five record/playback cycles completed

## Exact Serial Output

```text
[HW-005] CHIP_ID_1=0x83 CHIP_ID_2=0x11 VERSION=0x01
[HW-005][PASS] ES8311_ID_REGISTERS_READ
[HW-005][PASS] ES8311_INITIALIZED
[HW-005][PASS] I2S_INITIALIZED
[HW-005] AUDIO_TEST_READY
[HW-005] TONE=440Hz VOLUME=10 DURATION_MS=1000
[HW-005] TONE=880Hz VOLUME=20 DURATION_MS=1000
[HW-005][PASS] TONE_OUTPUT_PATH
[HW-005][PASS] VOLUME_AND_MUTE_CONTROLS
[HW-005] CAPTURE PEAK=36 RMS=5.04
[HW-005][PASS] MICROPHONE_CAPTURE_STREAM
[HW-005][FAIL] MICROPHONE_INPUT_NONZERO
[HW-005][PASS] MICROPHONE_INPUT_NOT_SATURATED
[HW-005][PASS] RECORDED_AUDIO_PLAYBACK_STREAM
[HW-005] RECORDING_DISCARDED=true
```

## Observations

The codec reported identity bytes `0x83`, `0x11`, and `0x01`. Tone generation,
volume/mute control writes, five-second capture allocation/readback, and
playback all completed. The microphone level was near silence in the first run;
operator stimulus and accessory connection must be confirmed before diagnosing
the input path.

## Defects and Follow-up

- Audible acceptance requires operator review with a connected speaker and a
  known acoustic stimulus.
- Audio recordings are intentionally not stored in the repository.
- Microphone input is currently below the automated non-zero threshold.
