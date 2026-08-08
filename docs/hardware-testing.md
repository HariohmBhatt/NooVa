# ESP32-S3 Hardware Test Plan

Validation plan for the Waveshare ESP32-S3 Touch LCD 3.5. This document
defines what each test must prove before component-specific firmware is
considered usable.

## Scope

The initial board bring-up has already verified that PlatformIO can build and
upload firmware, and that the board identifies as an ESP32-S3 with 8 MB
embedded PSRAM. The following component tests are planned:

| Test ID | Component | Device under test |
| --- | --- | --- |
| HW-001 | Display | ST7796, 320 x 480 LCD and backlight |
| HW-002 | Touch | FT6336 capacitive touch controller |
| HW-003 | Wi-Fi | ESP32-S3 2.4 GHz Wi-Fi radio |
| HW-004 | IMU | QMI8658 accelerometer and gyroscope |
| HW-005 | Audio | ES8311 codec, microphone, and speaker path |
| HW-006 | SD card | Onboard TF card interface |

The official [Waveshare ESP32-S3-Touch-LCD-3.5 wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5)
and its schematic are the source of truth for GPIOs, buses, addresses,
electrical levels, and peripheral initialization. This plan deliberately does
not duplicate pin numbers so that a stale pin map cannot silently become
firmware.

## Test Rules

1. Run one component test firmware at a time.
2. Close Arduino Serial Monitor and all other serial tools before uploading.
3. Record the exact firmware commit, PlatformIO environment, board revision,
   connected accessories, and test output.
4. Do not store Wi-Fi credentials, audio recordings, or other private data in
   the repository.
5. Do not format the SD card automatically. The test may create and remove a
   dedicated test directory only.
6. Start audio tests at the lowest safe volume and increase it gradually.
7. A test is `PASS` only when every acceptance item in its section passes.
   Use `FAIL` for a reproducible defect and `BLOCKED` for missing hardware,
   wiring, media, credentials, or an unresolved pin-map question.

## Common Procedure

1. Connect the board over USB and identify its port with `pio device list`.
2. Confirm the test firmware uses the official Waveshare pin map.
3. Build and upload the selected PlatformIO environment.
4. Reset the board and open the serial monitor at 115200 baud.
5. Capture the startup report and all test results.
6. Repeat a failed test once after a power cycle. Do not hide an intermittent
   failure by repeatedly resetting the board.
7. Save the result using the report template below.

Every test firmware should print a stable header with the test ID, firmware
version, board identification, and the configured peripheral parameters.

## HW-001 Display Test

### Objective

Verify the complete ST7796 display path: controller reset, bus writes, pixel
addressing, color format, geometry, orientation, and backlight control.

### Features to verify

- ST7796 initializes and exits reset without an error.
- The full 320 x 480 addressable area can be written.
- The selected color format produces correct primary and secondary colors.
- Text and graphics render at the top-left, center, bottom-right, and all
  screen edges without clipping.
- Rotation or orientation settings produce the intended width, height, and
  coordinate mapping.
- The backlight can be enabled, disabled, and set through its supported
  control path without changing pixel data.
- Repeated full-screen updates do not produce tearing, persistent artifacts,
  or a progressive loss of pixels.

### Procedure

1. Initialize the display using the official Waveshare ST7796 configuration.
2. Print the detected logical width and height.
3. Show full-screen black, white, red, green, blue, and a neutral gray in
   sequence. Inspect every pixel area for stuck, missing, or incorrect colors.
4. Draw a one-pixel border, a regular grid, diagonals, corner markers, and
   coordinate labels.
5. Repeat the pattern for every supported rotation used by the firmware.
6. Toggle the backlight and test its minimum, middle, and maximum intended
   settings.
7. Run a repeated color and geometry update for at least ten minutes while
   watching for artifacts or instability.

### Acceptance criteria

- Initialization reports success and the reported geometry is 320 x 480 in
  the default orientation.
- All color fields match their requested colors across the entire panel.
- Corner markers and borders are complete and correctly mapped for each
  supported orientation.
- Backlight transitions work without a reset, bus error, or visible flicker
  outside the transition itself.
- The ten-minute update run completes without a crash or persistent display
  corruption.

### Evidence

Record the display initialization log, orientation results, and photographs of
the color and geometry patterns if visual defects are found.

## HW-002 Touch Test

### Objective

Verify the complete FT6336 capacitive touch path: controller communication,
single-touch coordinates, multi-touch reporting, orientation mapping, and
event stability.

### Features to verify

- FT6336 initializes and reports a valid controller response.
- Touch press, movement, and release events are reported in order.
- Coordinates cover the complete usable display area, including all four
  corners and the center.
- Touch coordinates map correctly after every display orientation change.
- The controller reports simultaneous contacts supported by the FT6336, up to
  its documented contact limit.
- Contact IDs remain stable during a gesture and are released cleanly.
- No touch event is generated while the panel is untouched.
- Short taps, long presses, rapid taps, edge touches, and diagonal swipes are
  all distinguishable.

### Procedure

1. Initialize the FT6336 using the official Waveshare bus and address
   configuration.
2. Print every event with timestamp, event type, contact ID, and raw and
   mapped coordinates.
3. Tap each corner, the center, and the midpoint of every edge.
4. Draw slow horizontal, vertical, and diagonal lines with one finger.
5. Perform short taps, long presses, rapid taps, and a press-drag-release
   gesture.
6. Place the supported number of fingers on the panel, move them separately,
   then release them in a different order.
7. Repeat the single-touch and gesture checks under every display rotation.
8. Leave the panel untouched for at least five minutes and count any phantom
   events.

### Acceptance criteria

- Initialization succeeds without repeated bus errors.
- Every intentional touch produces one press and one release for its contact.
- Coordinates are monotonic and correctly mapped at the edges and center,
  with no systematic axis swap, inversion, or offset.
- Multi-touch contact IDs do not merge, duplicate, or remain stuck after
  release.
- No phantom events occur during the idle observation period.

### Evidence

Save the raw event log for the corner, edge, diagonal, and multi-touch cases.
Record the chosen display rotation and the observed coordinate bounds.

## HW-003 Wi-Fi Test

### Objective

Verify the ESP32-S3 2.4 GHz Wi-Fi radio in station mode, including scanning,
association, DHCP, local network traffic, DNS, disconnect handling, and
reconnection. Access-point mode is a separate optional check.

### Features to verify

- The radio can scan and report nearby 2.4 GHz networks.
- Station mode can associate with a test network using credentials supplied
  outside version control.
- DHCP assigns a valid address, gateway, and DNS server.
- DNS resolution works after association.
- A TCP or HTTP request completes against a known local or internet endpoint.
- RSSI and connection status can be read while connected.
- A forced disconnect is detected and the firmware reconnects without a
  reboot.
- The firmware handles an unavailable access point without blocking the main
  loop indefinitely.
- Optional: access-point mode starts, accepts a client, and transfers a small
  known payload.

### Procedure

1. Scan for networks and print channel, RSSI, and encryption information.
2. Connect to a dedicated 2.4 GHz test network. Do not use credentials in
   source files or committed configuration.
3. Print association status, assigned IP, gateway, DNS, channel, and RSSI.
4. Resolve a known hostname and perform a small request to a controlled
   endpoint.
5. Disconnect the access point or move out of range, and record the detected
   transition.
6. Restore the network and verify automatic reconnection.
7. Repeat the connect and disconnect cycle at least five times.
8. For the optional AP test, start AP mode, connect one client, exchange a
   known payload, then stop AP mode cleanly.

### Acceptance criteria

- Scan results contain known nearby 2.4 GHz networks with plausible RSSI and
  channel data.
- Station connection succeeds and all network parameters are valid.
- DNS and the controlled TCP or HTTP transaction succeed.
- Disconnects are reported promptly, and all five reconnection cycles recover
  without manual reset.
- A failed connection attempt times out or retries without starving the
  firmware.
- Optional AP mode passes client association and payload transfer.

### Evidence

Record the network test conditions, scan output, connection time, IP details,
RSSI range, request result, and reconnection results. Redact SSIDs if needed.

## HW-004 IMU Test

### Objective

Verify the QMI8658 communication path, accelerometer, gyroscope, data-ready
behavior, axis mapping, motion response, and temperature reporting if exposed
by the selected driver.

### Features to verify

- QMI8658 initializes and reports the expected device identity.
- Accelerometer returns three continuously changing axes.
- Gyroscope returns three continuously changing axes.
- Data-ready handling prevents stale or duplicated samples.
- Configured output data rate and measurement ranges match the printed
  configuration.
- Accelerometer magnitude is approximately 1 g while stationary, subject to
  sensor tolerance and board orientation.
- A stationary gyroscope reports values near zero, subject to bias.
- Each physical rotation changes the expected gyro axis and sign.
- Each board orientation changes the expected accelerometer axis and sign.
- Temperature is reported when supported by the QMI8658 driver.

### Procedure

1. Initialize the QMI8658 using the official Waveshare sensor bus
   configuration.
2. Print device identity, ranges, output data rate, and data-ready status.
3. Capture a stationary sample set for each sensor and calculate min, max,
   average, and sample interval.
4. Place the board flat, then on each of its six faces. Record the dominant
   acceleration axis and sign for each position.
5. Rotate the board separately around each physical axis and verify the
   corresponding gyroscope response.
6. Leave the board stationary for a drift observation and record gyro bias and
   acceleration magnitude.
7. Compare reported sample intervals with the configured output data rate.

### Acceptance criteria

- Identity and configuration match the intended QMI8658 setup.
- All six motion axes produce valid samples with no frozen or duplicated
  stream.
- The six-face test establishes a documented axis/sign mapping.
- Static acceleration is consistent with gravity within the sensor's expected
  tolerance, and static gyro output shows only expected bias and noise.
- Motion is reflected on the expected axis without unexplained cross-axis
  behavior.
- Sample timing is stable enough for the future application workload.

### Evidence

Save raw samples and summary statistics for stationary, six-face, and rotation
tests. Record the final axis/sign mapping separately from the driver code.

## HW-005 Audio Test

### Objective

Verify the ES8311 codec, I2S clocks and data paths, microphone input, speaker
output, volume and mute controls, and record/playback integrity.

### Features to verify

- ES8311 initializes and reports a valid codec response.
- Speaker output produces a clean low-volume test tone.
- Volume changes and mute take effect without a reset or stuck output.
- The microphone produces non-zero input for a known acoustic stimulus.
- Recorded audio can be played back through the speaker.
- The record and playback paths use the intended sample rate, bit depth,
  channel count, and I2S pin mapping from the official Waveshare demo or
  schematic.
- Input does not remain saturated when the microphone is quiet.
- A five-second record/playback cycle completes without underrun, overrun, or
  memory corruption.

### Procedure

1. Initialize the ES8311 and I2S using the official Waveshare configuration.
2. Start at the lowest safe speaker volume.
3. Play a short 440 Hz tone, then a low-amplitude frequency sweep. Confirm
   that the speaker responds without audible clipping or unexpected noise.
4. Change volume through low, medium, and high safe levels. Test mute and
   unmute between tones.
5. Record five seconds of silence and then five seconds of speech or a known
   tone from a fixed distance.
6. Calculate input peak, average level, and clipping count for each recording.
7. Play the recording back and compare its timing, content, and channel
   behavior with the source.
8. Repeat the record/playback cycle at least five times.

### Acceptance criteria

- Codec and I2S initialization succeed without repeated errors.
- Tone, sweep, volume, and mute controls behave as commanded.
- Microphone input changes with the stimulus and is not permanently silent or
  saturated.
- Playback contains the recorded stimulus with no repeatable underrun,
  overrun, reset, or memory fault.
- All five record/playback cycles complete successfully.
- The selected audio parameters are printed and match the verified board
  configuration.

### Evidence

Record the codec initialization log, audio parameters, peak and clipping
statistics, and any audible defect. Do not commit raw recordings unless they
are intentionally non-private test fixtures.

## HW-006 SD Card Test

### Objective

Verify the TF card interface, FAT32 mounting, directory and file operations,
binary data integrity, remount behavior, and safe handling of a missing card.

### Features to verify

- The card mounts through the official Waveshare SD interface configuration.
- Card type, capacity, and filesystem information are reported.
- Directory creation, enumeration, and removal work.
- Text and binary files can be created, written, read, renamed, and deleted.
- Data read back from the card matches data written to it.
- A larger sequential transfer completes and its throughput is measured.
- Unmount and remount preserve the expected files and contents.
- A missing or removed card produces a controlled error instead of a crash or
  endless blocking loop.

### Procedure

1. Use a known-good FAT32 card. Do not format it from firmware.
2. Mount it using the official Waveshare SD example's bus mode and settings.
3. Print card type, capacity, mount path, and filesystem status.
4. Create a dedicated directory such as `/nova-hw-test`.
5. Write a small text file and a deterministic binary pattern into that
   directory.
6. Read both files back and compare their exact lengths and contents.
7. Rename, enumerate, and delete the files and directory.
8. Write and read a larger sequential test file, recording elapsed time and
   throughput. Remove it after verification.
9. Unmount, remount, and repeat the read check.
10. Repeat the mount with no card inserted and verify controlled failure.

### Acceptance criteria

- A FAT32 card mounts and reports plausible capacity.
- All small-file and directory operations succeed.
- Binary readback exactly matches the generated pattern.
- The larger transfer completes without corruption, reset, or timeout, and
  its measured throughput is recorded for future comparison.
- Remount preserves expected state.
- Missing-card handling returns a clear error and leaves the rest of the
  firmware responsive.
- The test leaves no unintended files outside its dedicated test directory.

### Evidence

Record card make/capacity, filesystem type, mount log, integrity result,
transfer sizes and timings, remount result, and missing-card behavior.

## Test Report Template

Copy this template into a dated report when executing a test:

```text
Test ID:
Component:
Date:
Firmware commit:
PlatformIO environment:
Board serial/MAC:
Accessories and conditions:

Steps executed:

Observed output:

Acceptance results:
- [ ] Passed
- [ ] Failed
- [ ] Blocked

Defects or follow-up work:
Evidence files:
```

## Implementation Order

The tests should be implemented one at a time in this order:

1. Display, because it provides visual feedback for later tests.
2. Touch, because it depends on display coordinate orientation.
3. IMU, because it is independent and has a simple serial data result.
4. SD card, because it provides a repeatable storage integrity test.
5. Audio, because it requires careful volume and accessory handling.
6. Wi-Fi, because credentials and network conditions must be supplied by the
   operator.

Each implementation should have a dedicated PlatformIO environment and a
small, independently runnable test application. The existing bring-up
diagnostic remains the baseline environment and should not be replaced until
all component tests have passed.
