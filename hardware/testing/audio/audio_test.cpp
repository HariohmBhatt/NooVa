#include <Arduino.h>
#include <Wire.h>

#include "driver/i2s.h"

#include <cmath>

namespace {

// These values are copied from Waveshare's official 04_es8311_example.
constexpr uint8_t kI2cSdaPin = 8;
constexpr uint8_t kI2cSclPin = 7;
constexpr uint8_t kCodecAddress = 0x18;
constexpr i2s_port_t kI2sPort = I2S_NUM_0;
constexpr int kI2sMclkPin = 12;
constexpr int kI2sBclkPin = 13;
constexpr int kI2sLrckPin = 15;
constexpr int kI2sDataOutPin = 16;
constexpr int kI2sDataInPin = 14;
constexpr uint32_t kSampleRate = 44100;
constexpr uint32_t kMclkFrequency = kSampleRate * 256;
constexpr uint8_t kBitsPerSample = 16;
constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kSerialConnectTimeoutMs = 3000;
constexpr uint32_t kStartupCaptureDelayMs = 2000;
constexpr uint32_t kToneDurationMs = 1000;
constexpr uint32_t kCaptureDurationMs = 5000;
constexpr uint8_t kSafeVolumePercent = 10;
constexpr uint8_t kTestVolumePercent = 70;
constexpr size_t kToneFramesPerChunk = 256;
constexpr size_t kCaptureChunkBytes = 4096;
constexpr float kToneAmplitude = 0.20F;
constexpr float kPi = 3.14159265358979323846F;

// ES8311 registers used by the official driver configuration.
constexpr uint8_t kResetRegister = 0x00;
constexpr uint8_t kClock1Register = 0x01;
constexpr uint8_t kClock2Register = 0x02;
constexpr uint8_t kClock3Register = 0x03;
constexpr uint8_t kClock4Register = 0x04;
constexpr uint8_t kClock5Register = 0x05;
constexpr uint8_t kClock6Register = 0x06;
constexpr uint8_t kClock7Register = 0x07;
constexpr uint8_t kClock8Register = 0x08;
constexpr uint8_t kSerialInputRegister = 0x09;
constexpr uint8_t kSerialOutputRegister = 0x0A;
constexpr uint8_t kAnalogPowerRegister = 0x0D;
constexpr uint8_t kAnalogEnableRegister = 0x0E;
constexpr uint8_t kDacPowerRegister = 0x12;
constexpr uint8_t kOutputDriveRegister = 0x13;
constexpr uint8_t kMicSelectRegister = 0x14;
constexpr uint8_t kMicGainRegister = 0x16;
constexpr uint8_t kAdcVolumeRegister = 0x17;
constexpr uint8_t kAdcFilterRegister = 0x1C;
constexpr uint8_t kDacMuteRegister = 0x31;
constexpr uint8_t kDacVolumeRegister = 0x32;
constexpr uint8_t kDacFilterRegister = 0x37;
constexpr uint8_t kChipId1Register = 0xFD;
constexpr uint8_t kChipId2Register = 0xFE;
constexpr uint8_t kChipVersionRegister = 0xFF;

bool gAutomatedChecksPassed = true;
bool gCodecReady = false;
bool gI2sReady = false;
uint8_t* gCaptureBuffer = nullptr;

void printResult(const char* check, bool passed) {
  Serial.printf("[HW-005][%s] %s\n", passed ? "PASS" : "FAIL", check);
  gAutomatedChecksPassed = gAutomatedChecksPassed && passed;
}

bool writeCodecRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kCodecAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission(true) == 0;
}

bool readCodecRegister(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(kCodecAddress);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0 ||
      Wire.requestFrom(static_cast<uint8_t>(kCodecAddress), static_cast<uint8_t>(1)) != 1 ||
      !Wire.available()) {
    return false;
  }
  value = Wire.read();
  return true;
}

bool configureCodecClock() {
  uint8_t registerValue = 0;
  if (!writeCodecRegister(kClock1Register, 0x3F) ||
      !readCodecRegister(kClock2Register, registerValue) ||
      !writeCodecRegister(kClock2Register, registerValue & 0x07) ||
      !writeCodecRegister(kClock3Register, 0x10) ||
      !writeCodecRegister(kClock4Register, 0x10) ||
      !writeCodecRegister(kClock5Register, 0x00) ||
      !readCodecRegister(kClock6Register, registerValue)) {
    return false;
  }

  registerValue = (registerValue & 0xE0) | 0x03;
  if (!writeCodecRegister(kClock6Register, registerValue) ||
      !readCodecRegister(kClock7Register, registerValue)) {
    return false;
  }
  return writeCodecRegister(kClock7Register, registerValue & 0xC0) &&
         writeCodecRegister(kClock8Register, 0xFF);
}

bool configureCodecFormat() {
  uint8_t registerValue = 0;
  if (!readCodecRegister(kResetRegister, registerValue) ||
      !writeCodecRegister(kResetRegister, registerValue & 0xBF)) {
    return false;
  }
  return writeCodecRegister(kSerialInputRegister, 0x0C) &&
         writeCodecRegister(kSerialOutputRegister, 0x0C);
}

bool initializeCodec() {
  if (!writeCodecRegister(kResetRegister, 0x1F)) {
    return false;
  }
  delay(20);
  if (!writeCodecRegister(kResetRegister, 0x00) ||
      !writeCodecRegister(kResetRegister, 0x80) || !configureCodecClock() ||
      !configureCodecFormat()) {
    return false;
  }

  return writeCodecRegister(kAnalogPowerRegister, 0x01) &&
         writeCodecRegister(kAnalogEnableRegister, 0x02) &&
         writeCodecRegister(kDacPowerRegister, 0x00) &&
         writeCodecRegister(kOutputDriveRegister, 0x10) &&
         writeCodecRegister(kAdcFilterRegister, 0x6A) &&
         writeCodecRegister(kDacFilterRegister, 0x08) &&
         writeCodecRegister(kMicGainRegister, 0x00) &&
         writeCodecRegister(kAdcVolumeRegister, 0xC8) &&
         writeCodecRegister(kMicSelectRegister, 0x1A);
}

bool setCodecVolume(uint8_t volumePercent) {
  const uint8_t registerValue = volumePercent == 0
                                    ? 0
                                    : static_cast<uint8_t>((volumePercent * 256 / 100) - 1);
  return writeCodecRegister(kDacVolumeRegister, registerValue);
}

bool setCodecMute(bool muted) {
  uint8_t registerValue = 0;
  if (!readCodecRegister(kDacMuteRegister, registerValue)) {
    return false;
  }
  registerValue = muted ? registerValue | 0x60 : registerValue & ~0x60;
  return writeCodecRegister(kDacMuteRegister, registerValue);
}

bool initializeI2s() {
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  config.sample_rate = kSampleRate;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;
  config.use_apll = true;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = kMclkFrequency;

  const i2s_pin_config_t pins = {
      .mck_io_num = kI2sMclkPin,
      .bck_io_num = kI2sBclkPin,
      .ws_io_num = kI2sLrckPin,
      .data_out_num = kI2sDataOutPin,
      .data_in_num = kI2sDataInPin,
  };
  if (i2s_driver_install(kI2sPort, &config, 0, nullptr) != ESP_OK ||
      i2s_set_pin(kI2sPort, &pins) != ESP_OK) {
    return false;
  }
  i2s_zero_dma_buffer(kI2sPort);
  return true;
}

void printHeader() {
  Serial.println();
  Serial.println("[HW-005] AUDIO_TEST_START");
  Serial.println("[HW-005] CODEC=ES8311 ADDRESS=0x18");
  Serial.printf("[HW-005] I2C_SDA=%u I2C_SCL=%u\n", kI2cSdaPin, kI2cSclPin);
  Serial.printf("[HW-005] I2S_MCLK=%d I2S_BCLK=%d I2S_LRCK=%d DATA_OUT=%d DATA_IN=%d\n",
                kI2sMclkPin, kI2sBclkPin, kI2sLrckPin, kI2sDataOutPin,
                kI2sDataInPin);
  Serial.printf("[HW-005] SAMPLE_RATE=%lu BITS=%u MCLK=%lu\n", kSampleRate,
                kBitsPerSample, kMclkFrequency);
}

void printCodecIdentity() {
  uint8_t chipId1 = 0;
  uint8_t chipId2 = 0;
  uint8_t version = 0;
  const bool passed = readCodecRegister(kChipId1Register, chipId1) &&
                      readCodecRegister(kChipId2Register, chipId2) &&
                      readCodecRegister(kChipVersionRegister, version);
  Serial.printf("[HW-005] CHIP_ID_1=0x%02X CHIP_ID_2=0x%02X VERSION=0x%02X\n",
                chipId1, chipId2, version);
  printResult("ES8311_ID_REGISTERS_READ", passed);
}

bool writeTone(float frequencyHz, uint32_t durationMs) {
  int16_t samples[kToneFramesPerChunk * 2] = {};
  const uint32_t totalFrames = kSampleRate * durationMs / 1000;
  uint32_t frameOffset = 0;
  while (frameOffset < totalFrames) {
    const size_t frameCount = std::min<size_t>(kToneFramesPerChunk, totalFrames - frameOffset);
    for (size_t frame = 0; frame < frameCount; ++frame) {
      const float phase = 2.0F * kPi * frequencyHz * (frameOffset + frame) / kSampleRate;
      const int16_t sample = static_cast<int16_t>(std::sin(phase) * 32767.0F * kToneAmplitude);
      samples[frame * 2] = sample;
      samples[frame * 2 + 1] = sample;
    }
    size_t bytesWritten = 0;
    if (i2s_write(kI2sPort, samples, frameCount * sizeof(samples[0]) * 2,
                  &bytesWritten, portMAX_DELAY) != ESP_OK ||
        bytesWritten != frameCount * sizeof(samples[0]) * 2) {
      return false;
    }
    frameOffset += frameCount;
  }
  return true;
}

void runToneTest() {
  Serial.println("[HW-005] TONE_TEST_START manual_audio_review_required=true");
  const bool lowVolume = setCodecVolume(kSafeVolumePercent) && writeTone(440.0F, kToneDurationMs);
  Serial.printf("[HW-005] TONE=440Hz VOLUME=%u DURATION_MS=%lu\n",
                kSafeVolumePercent, kToneDurationMs);
  const bool testVolume = setCodecVolume(kTestVolumePercent) && writeTone(880.0F, kToneDurationMs);
  Serial.printf("[HW-005] TONE=880Hz VOLUME=%u DURATION_MS=%lu\n",
                kTestVolumePercent, kToneDurationMs);
  const bool muteSet = setCodecMute(true);
  const bool mutedTone = writeTone(440.0F, kToneDurationMs);
  const bool unmuteSet = setCodecMute(false);
  const bool restoredTone = writeTone(440.0F, kToneDurationMs);
  printResult("TONE_OUTPUT_PATH", lowVolume && testVolume && mutedTone && restoredTone);
  printResult("VOLUME_AND_MUTE_CONTROLS", muteSet && unmuteSet);
  setCodecVolume(kSafeVolumePercent);
}

bool captureAudio(uint8_t* buffer, size_t bufferSize, int32_t& peak, float& rms) {
  size_t totalBytes = 0;
  int64_t squareSum = 0;
  uint64_t sampleCount = 0;
  peak = 0;
  while (totalBytes < bufferSize) {
    size_t bytesRead = 0;
    const size_t requestSize = std::min(kCaptureChunkBytes, bufferSize - totalBytes);
    if (i2s_read(kI2sPort, buffer + totalBytes, requestSize, &bytesRead,
                 pdMS_TO_TICKS(1000)) != ESP_OK || bytesRead == 0) {
      return false;
    }
    const int16_t* samples = reinterpret_cast<const int16_t*>(buffer + totalBytes);
    for (size_t index = 0; index < bytesRead / sizeof(int16_t); ++index) {
      const int32_t value = samples[index];
      peak = std::max(peak, std::abs(value));
      squareSum += static_cast<int64_t>(value) * value;
      ++sampleCount;
    }
    totalBytes += bytesRead;
  }
  rms = sampleCount == 0 ? 0.0F : std::sqrt(static_cast<float>(squareSum) / sampleCount);
  return true;
}

void runRecordPlayback() {
  constexpr size_t kCaptureBytes = kSampleRate * kCaptureDurationMs / 1000 * 2 * sizeof(int16_t);
  Serial.printf("[HW-005] RECORD_PLAYBACK_START DURATION_MS=%lu BUFFER_BYTES=%lu\n",
                kCaptureDurationMs, kCaptureBytes);
  Serial.println("[HW-005] Speak or play a known tone near the microphone now.");
  gCaptureBuffer = static_cast<uint8_t*>(ps_malloc(kCaptureBytes));
  if (!gCaptureBuffer) {
    printResult("CAPTURE_BUFFER_ALLOCATED", false);
    return;
  }
  printResult("CAPTURE_BUFFER_ALLOCATED", true);

  int32_t peak = 0;
  float rms = 0.0F;
  const bool captured = captureAudio(gCaptureBuffer, kCaptureBytes, peak, rms);
  Serial.printf("[HW-005] CAPTURE PEAK=%ld RMS=%.2f\n", peak, rms);
  printResult("MICROPHONE_CAPTURE_STREAM", captured);
  printResult("MICROPHONE_INPUT_NONZERO", captured && peak > 500);
  printResult("MICROPHONE_INPUT_NOT_SATURATED", captured && peak < 32760);

  bool played = false;
  if (captured) {
    i2s_zero_dma_buffer(kI2sPort);
    size_t bytesPlayed = 0;
    size_t offset = 0;
    while (offset < kCaptureBytes) {
      const size_t requestSize = std::min(kCaptureChunkBytes, kCaptureBytes - offset);
      size_t written = 0;
      if (i2s_write(kI2sPort, gCaptureBuffer + offset, requestSize, &written,
                    portMAX_DELAY) != ESP_OK || written == 0) {
        break;
      }
      offset += written;
      bytesPlayed += written;
    }
    played = bytesPlayed == kCaptureBytes;
  }
  printResult("RECORDED_AUDIO_PLAYBACK_STREAM", played);
  free(gCaptureBuffer);
  gCaptureBuffer = nullptr;
  Serial.println("[HW-005] RECORDING_DISCARDED=true");
}

void printSummary() {
  Serial.printf("[HW-005] AUTOMATED_CHECKS=%s\n",
                gAutomatedChecksPassed ? "PASS" : "FAIL");
  Serial.printf("[HW-005] CODEC_READY=%s I2S_READY=%s\n",
                gCodecReady ? "YES" : "NO", gI2sReady ? "YES" : "NO");
}

void printHelp() {
  Serial.println("[HW-005] COMMANDS: t=tone volume mute, r=record/playback, s=summary, h=help");
}

void handleCommand() {
  while (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    if (command == 't') {
      runToneTest();
    } else if (command == 'r') {
      runRecordPlayback();
    } else if (command == 's') {
      printSummary();
    } else if (command == 'h') {
      printHelp();
    }
  }
}

void haltAfterFailure() {
  Serial.println("[HW-005][FAIL] AUDIO_TEST_HALTED");
  while (true) {
    delay(1000);
    Serial.println("[HW-005][FAIL] RESET_REQUIRED");
  }
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaudRate);
  const uint32_t serialStartedAt = millis();
  while (!Serial && millis() - serialStartedAt < kSerialConnectTimeoutMs) {
    delay(10);
  }

  delay(kStartupCaptureDelayMs);
  printHeader();
  Wire.begin(kI2cSdaPin, kI2cSclPin);
  printCodecIdentity();
  gCodecReady = initializeCodec();
  printResult("ES8311_INITIALIZED", gCodecReady);
  if (!gCodecReady) {
    haltAfterFailure();
  }
  gI2sReady = initializeI2s();
  printResult("I2S_INITIALIZED", gI2sReady);
  if (!gI2sReady) {
    haltAfterFailure();
  }
  setCodecVolume(kSafeVolumePercent);
  printResult("ES8311_UNMUTED", setCodecMute(false));
  Serial.println("[HW-005] AUDIO_TEST_READY");
  printHelp();
  printSummary();
}

void loop() {
  handleCommand();
  delay(10);
}
