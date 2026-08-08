#include <Arduino.h>
#include <SensorQMI8658.hpp>

#include <algorithm>
#include <cmath>

namespace {

// These values are copied from Waveshare's official 06_qmi8658_getdata example.
constexpr uint8_t kSensorSdaPin = 8;
constexpr uint8_t kSensorSclPin = 7;
constexpr int8_t kSensorIrqPin = -1;
constexpr uint8_t kSensorAddress = QMI8658_L_SLAVE_ADDRESS;

constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kSerialConnectTimeoutMs = 3000;
constexpr uint32_t kStationaryDurationMs = 5000;
constexpr uint32_t kMotionDurationMs = 10000;
constexpr uint32_t kFaceDurationMs = 2500;
constexpr uint32_t kSampleReportIntervalMs = 250;
constexpr uint32_t kLoopDelayMs = 5;
constexpr uint8_t kMinimumSampleCount = 25;
constexpr float kGravityMinimumG = 0.80F;
constexpr float kGravityMaximumG = 1.20F;
constexpr float kStationaryGyroMaximumDps = 30.0F;
constexpr float kMotionAccelRangeMinimumG = 0.20F;
constexpr float kMotionGyroRangeMinimumDps = 5.0F;

SensorQMI8658 qmi;
bool gAutomatedChecksPassed = true;
bool gSensorReady = false;

struct SampleStats {
  uint32_t samples;
  uint32_t dataNotReady;
  uint32_t readErrors;
  float accelMagnitudeSum;
  float gyroMagnitudeSum;
  float accelMagnitudeMin;
  float accelMagnitudeMax;
  float gyroMagnitudeMin;
  float gyroMagnitudeMax;
  float accelMin[3];
  float accelMax[3];
  float gyroMin[3];
  float gyroMax[3];
};

void printResult(const char* check, bool passed) {
  Serial.printf("[HW-004][%s] %s\n", passed ? "PASS" : "FAIL", check);
  gAutomatedChecksPassed = gAutomatedChecksPassed && passed;
}

void resetStats(SampleStats& stats) {
  stats = {};
  stats.accelMagnitudeMin = 1000.0F;
  stats.gyroMagnitudeMin = 1000.0F;
  for (uint8_t axis = 0; axis < 3; ++axis) {
    stats.accelMin[axis] = 1000.0F;
    stats.gyroMin[axis] = 1000.0F;
    stats.accelMax[axis] = -1000.0F;
    stats.gyroMax[axis] = -1000.0F;
  }
}

float magnitude(const IMUdata& value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

void updateAxisStats(float value, float& minimum, float& maximum) {
  minimum = std::min(minimum, value);
  maximum = std::max(maximum, value);
}

void updateStats(SampleStats& stats, const IMUdata& accel, const IMUdata& gyro) {
  const float accelMagnitude = magnitude(accel);
  const float gyroMagnitude = magnitude(gyro);
  const float accelValues[] = {accel.x, accel.y, accel.z};
  const float gyroValues[] = {gyro.x, gyro.y, gyro.z};

  ++stats.samples;
  stats.accelMagnitudeSum += accelMagnitude;
  stats.gyroMagnitudeSum += gyroMagnitude;
  updateAxisStats(accelMagnitude, stats.accelMagnitudeMin, stats.accelMagnitudeMax);
  updateAxisStats(gyroMagnitude, stats.gyroMagnitudeMin, stats.gyroMagnitudeMax);
  for (uint8_t axis = 0; axis < 3; ++axis) {
    updateAxisStats(accelValues[axis], stats.accelMin[axis], stats.accelMax[axis]);
    updateAxisStats(gyroValues[axis], stats.gyroMin[axis], stats.gyroMax[axis]);
  }
}

void printHeader() {
  Serial.println();
  Serial.println("[HW-004] IMU_TEST_START");
  Serial.println("[HW-004] SENSOR=QMI8658");
  Serial.printf("[HW-004] I2C_SDA=%u I2C_SCL=%u ADDRESS=0x%02X IRQ=%d\n",
                kSensorSdaPin, kSensorSclPin, kSensorAddress, kSensorIrqPin);
  Serial.println("[HW-004] ACCEL_RANGE=4G ACCEL_ODR=1000Hz ACCEL_LPF=MODE_0");
  Serial.println("[HW-004] GYRO_RANGE=64DPS GYRO_ODR=896.8Hz GYRO_LPF=MODE_3");
}

void printStats(const char* phase, const SampleStats& stats) {
  if (stats.samples == 0) {
    Serial.printf("[HW-004] STATS=%s SAMPLES=0 NOT_READY=%lu READ_ERRORS=%lu\n",
                  phase, stats.dataNotReady, stats.readErrors);
    return;
  }
  const float accelMean = stats.accelMagnitudeSum / stats.samples;
  const float gyroMean = stats.gyroMagnitudeSum / stats.samples;
  Serial.printf(
      "[HW-004] STATS=%s SAMPLES=%lu NOT_READY=%lu READ_ERRORS=%lu ACCEL_MAG_G=%.4f..%.4f MEAN=%.4f GYRO_MAG_DPS=%.4f..%.4f MEAN=%.4f\n",
      phase, stats.samples, stats.dataNotReady, stats.readErrors,
      stats.accelMagnitudeMin, stats.accelMagnitudeMax, accelMean,
      stats.gyroMagnitudeMin, stats.gyroMagnitudeMax, gyroMean);
  Serial.printf("[HW-004] AXIS_RANGE=%s ACCEL_X=%.4f..%.4f ACCEL_Y=%.4f..%.4f ACCEL_Z=%.4f..%.4f GYRO_X=%.4f..%.4f GYRO_Y=%.4f..%.4f GYRO_Z=%.4f..%.4f\n",
                phase, stats.accelMin[0], stats.accelMax[0], stats.accelMin[1],
                stats.accelMax[1], stats.accelMin[2], stats.accelMax[2],
                stats.gyroMin[0], stats.gyroMax[0], stats.gyroMin[1],
                stats.gyroMax[1], stats.gyroMin[2], stats.gyroMax[2]);
}

void collectSamples(uint32_t durationMs, bool printSamples, SampleStats& stats) {
  resetStats(stats);
  const uint32_t startedAt = millis();
  uint32_t lastReportAt = startedAt;

  while (millis() - startedAt < durationMs) {
    if (!qmi.getDataReady()) {
      ++stats.dataNotReady;
      delay(kLoopDelayMs);
      continue;
    }

    IMUdata accel;
    IMUdata gyro;
    if (!qmi.getAccelerometer(accel.x, accel.y, accel.z) ||
        !qmi.getGyroscope(gyro.x, gyro.y, gyro.z)) {
      ++stats.readErrors;
      delay(kLoopDelayMs);
      continue;
    }
    updateStats(stats, accel, gyro);
    if (printSamples && millis() - lastReportAt >= kSampleReportIntervalMs) {
      lastReportAt = millis();
      Serial.printf("[HW-004] SAMPLE ACCEL=%.4f,%.4f,%.4f GYRO=%.4f,%.4f,%.4f TEMP_C=%.2f\n",
                    accel.x, accel.y, accel.z, gyro.x, gyro.y, gyro.z,
                    qmi.getTemperature_C());
    }
    delay(kLoopDelayMs);
  }
}

void runStationaryTest() {
  Serial.printf("[HW-004] STATIONARY_TEST_START DURATION_MS=%lu\n",
                kStationaryDurationMs);
  Serial.println("[HW-004] Keep the board still on a stable surface.");
  delay(1000);

  SampleStats stats;
  collectSamples(kStationaryDurationMs, false, stats);
  printStats("STATIONARY", stats);
  if (stats.samples == 0) {
    printResult("STATIONARY_SAMPLE_COUNT", false);
    printResult("STATIONARY_ACCEL_MAGNITUDE", false);
    printResult("STATIONARY_GYRO_BIAS", false);
    return;
  }
  const float accelMean = stats.accelMagnitudeSum / stats.samples;
  const bool enoughSamples = stats.samples >= kMinimumSampleCount;
  const bool gravityValid = accelMean > kGravityMinimumG &&
                            accelMean < kGravityMaximumG;
  const bool gyroStationary = stats.gyroMagnitudeMax < kStationaryGyroMaximumDps;
  printResult("STATIONARY_SAMPLE_COUNT", enoughSamples);
  printResult("STATIONARY_ACCEL_MAGNITUDE", enoughSamples && gravityValid);
  printResult("STATIONARY_GYRO_BIAS", enoughSamples && gyroStationary);
}

void runMotionTest() {
  Serial.printf("[HW-004] MOTION_TEST_START DURATION_MS=%lu\n", kMotionDurationMs);
  Serial.println("[HW-004] Move and rotate the board through all three axes now.");
  delay(1000);

  SampleStats stats;
  collectSamples(kMotionDurationMs, true, stats);
  printStats("MOTION", stats);
  if (stats.samples == 0) {
    printResult("MOTION_ACCEL_RESPONSE", false);
    printResult("MOTION_GYRO_RESPONSE", false);
    return;
  }
  const float accelRange = std::max(
      {stats.accelMax[0] - stats.accelMin[0], stats.accelMax[1] - stats.accelMin[1],
       stats.accelMax[2] - stats.accelMin[2]});
  const float gyroRange = std::max(
      {stats.gyroMax[0] - stats.gyroMin[0], stats.gyroMax[1] - stats.gyroMin[1],
       stats.gyroMax[2] - stats.gyroMin[2]});
  Serial.printf("[HW-004] MOTION_RANGE ACCEL_G=%.4f GYRO_DPS=%.4f\n", accelRange,
                gyroRange);
  printResult("MOTION_ACCEL_RESPONSE", accelRange > kMotionAccelRangeMinimumG);
  printResult("MOTION_GYRO_RESPONSE", gyroRange > kMotionGyroRangeMinimumDps);
}

void runFaceTest() {
  Serial.printf("[HW-004] SIX_FACE_TEST_START FACE_DURATION_MS=%lu\n",
                kFaceDurationMs);
  Serial.println("[HW-004] Starting with face 1; rotate to each prompted face.");
  float faceMeans[6][3] = {};
  for (uint8_t face = 1; face <= 6; ++face) {
    Serial.printf("[HW-004] FACE=%u PLACE_BOARD_AND_HOLD\n", face);
    delay(1000);
    SampleStats stats;
  collectSamples(kFaceDurationMs, false, stats);
    if (stats.samples == 0) {
      printResult("FACE_CAPTURED", false);
      continue;
    }
    const float meanAccel = stats.accelMagnitudeSum / stats.samples;
    faceMeans[face - 1][0] = (stats.accelMin[0] + stats.accelMax[0]) / 2.0F;
    faceMeans[face - 1][1] = (stats.accelMin[1] + stats.accelMax[1]) / 2.0F;
    faceMeans[face - 1][2] = (stats.accelMin[2] + stats.accelMax[2]) / 2.0F;
    Serial.printf("[HW-004] FACE_RESULT=%u SAMPLES=%lu ACCEL_MAG_MEAN_G=%.4f AXIS_MEAN_G=%.4f,%.4f,%.4f\n",
                  face, stats.samples, meanAccel,
                  faceMeans[face - 1][0], faceMeans[face - 1][1],
                  faceMeans[face - 1][2]);
    printResult(face == 1 ? "FACE_1_CAPTURED" : "FACE_CAPTURED", stats.samples > 0);
  }
  float axisVariation[3] = {};
  for (uint8_t face = 1; face < 6; ++face) {
    for (uint8_t axis = 0; axis < 3; ++axis) {
      axisVariation[axis] = std::max(
          axisVariation[axis], std::fabs(faceMeans[face][axis] - faceMeans[0][axis]));
    }
  }
  Serial.printf("[HW-004] SIX_FACE_AXIS_VARIATION_G=%.4f,%.4f,%.4f\n",
                axisVariation[0], axisVariation[1], axisVariation[2]);
  printResult("SIX_FACE_ACCELERATION_VARIATION",
              std::max({axisVariation[0], axisVariation[1], axisVariation[2]}) > 0.50F);
  Serial.println("[HW-004] SIX_FACE_TEST_COMPLETE MANUAL_AXIS_REVIEW_REQUIRED=true");
}

void printSummary() {
  Serial.printf("[HW-004] AUTOMATED_CHECKS=%s\n",
                gAutomatedChecksPassed ? "PASS" : "FAIL");
  Serial.printf("[HW-004] SENSOR_READY=%s\n", gSensorReady ? "YES" : "NO");
}

void printHelp() {
  Serial.println("[HW-004] COMMANDS: s=stationary, m=motion, f=six-face, t=temperature, h=help");
}

void handleCommand() {
  while (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    if (command == 's') {
      runStationaryTest();
    } else if (command == 'm') {
      runMotionTest();
    } else if (command == 'f') {
      runFaceTest();
    } else if (command == 't') {
      Serial.printf("[HW-004] TEMPERATURE_C=%.2f\n", qmi.getTemperature_C());
    } else if (command == 'h') {
      printHelp();
    }
  }
}

void haltAfterFailure() {
  Serial.println("[HW-004][FAIL] IMU_TEST_HALTED");
  while (true) {
    delay(1000);
    Serial.println("[HW-004][FAIL] RESET_REQUIRED");
  }
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaudRate);
  const uint32_t serialStartedAt = millis();
  while (!Serial && millis() - serialStartedAt < kSerialConnectTimeoutMs) {
    delay(10);
  }

  printHeader();
  if (!qmi.begin(Wire, kSensorAddress, kSensorSdaPin, kSensorSclPin)) {
    printResult("QMI8658_INITIALIZED", false);
    haltAfterFailure();
  }
  printResult("QMI8658_INITIALIZED", true);
  const bool accelSelfTest = qmi.selfTestAccel();
  const bool gyroSelfTest = qmi.selfTestGyro();
  printResult("ACCELEROMETER_SELF_TEST", accelSelfTest);
  printResult("GYROSCOPE_SELF_TEST", gyroSelfTest);
  printResult("ACCELEROMETER_CONFIGURED",
              qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                                      SensorQMI8658::ACC_ODR_1000Hz,
                                      SensorQMI8658::LPF_MODE_0) == 0);
  printResult("GYROSCOPE_CONFIGURED",
              qmi.configGyroscope(SensorQMI8658::GYR_RANGE_64DPS,
                                  SensorQMI8658::GYR_ODR_896_8Hz,
                                  SensorQMI8658::LPF_MODE_3) == 0);
  printResult("ACCELEROMETER_ENABLED", qmi.enableAccelerometer());
  printResult("GYROSCOPE_ENABLED", qmi.enableGyroscope());
  Serial.printf("[HW-004] SCALES ACCEL=%.8f G_PER_COUNT GYRO=%.8f DPS_PER_COUNT\n",
                qmi.getAccelerometerScales(), qmi.getGyroscopeScales());
  gSensorReady = true;
  runStationaryTest();
  Serial.println("[HW-004] IMU_TEST_READY");
  printHelp();
  printSummary();
}

void loop() {
  handleCommand();
  delay(10);
}
