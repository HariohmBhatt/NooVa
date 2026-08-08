# Home AI Hub

**ESP32-S3 Touch Terminal + Home Server + Local Voice Assistant**  
_Working codename: NOVA_

**Project Blueprint — v0.1**  
**Date:** 8 August 2026

---

## 1. Project definition

Build a local-first home control and voice-assistant platform in which the **Waveshare ESP32-S3 Touch LCD 3.5** acts as the physical interface, while the **home server** acts as the intelligence and automation layer.

The finished system should provide:

- A responsive 3.5-inch touch dashboard.
- Live connection to the home server.
- Server health and service status.
- Home-device state and control.
- Push-to-talk voice interaction first.
- Wake-word interaction later.
- Local speech-to-text, intent routing, AI reasoning, and text-to-speech where practical.
- A clean path to add room nodes, relays, sensors, cameras, and other devices later.

The ESP32 should stay lightweight: it handles the UI, audio capture/playback, touch, local feedback, and networking. The home server performs heavy computation, integrations, device orchestration, speech processing, and AI.

### One-sentence architecture

**ESP32-S3 = face and ears; Home Server = brain; Home Assistant/MQTT = device control plane.**

---

## 2. Primary goals

1. Reliable ESP32 ↔ home-server link.
2. Useful touch dashboard.
3. Push-to-talk voice assistant, then wake word.
4. Safe home automation.
5. Local-first operation.
6. Maintainable engineering.

## 3. Non-goals for the first version

- Match Alexa far-field microphone performance.
- Support every smart-home protocol immediately.
- Run a large language model directly on the ESP32.
- Expose the ESP32 directly to the public internet.
- Control high-voltage mains directly from the main ESP32 board.
- Implement camera/vision before the core voice and control path is stable.

## 4. Hardware baseline

| Capability | Hardware |
|---|---|
| MCU | ESP32-S3R8, dual-core up to 240 MHz |
| RAM | 512 KB SRAM + 8 MB PSRAM |
| Flash | 16 MB |
| Display | 3.5-inch IPS, 320 × 480 |
| Display controller | ST7796S |
| Touch | FT6336 capacitive touch |
| Connectivity | 2.4 GHz Wi-Fi + Bluetooth 5 LE |
| Audio codec | ES8311 |
| Microphone | Onboard microphone |
| Speaker | 6 Ω / 1 W speaker supplied |
| IMU | QMI8658 6-axis |
| RTC | PCF85063 |
| Power management | AXP2101 |
| Storage | microSD / TF slot |
| Expansion | GPIO, I2C, UART, USB |
| Camera | OV2640 / OV5640-compatible interface |
| Battery | 3.7 V lithium battery via MX1.25 2-pin connector |

## 5. System architecture

```text
                  ┌────────────────────────────┐
                  │  ESP32-S3 TOUCH TERMINAL   │
                  │ LCD + Touch + Mic + Audio  │
                  │ Wi-Fi + Local UI State     │
                  └─────────────┬──────────────┘
                                │
                         Local Wi-Fi/LAN
                     WebSocket / HTTPS
                                │
                                ▼
              ┌─────────────────────────────────┐
              │         HOME SERVER             │
              │ Hub Gateway / FastAPI           │
              │ Voice Pipeline + Intent Router  │
              │ Home Assistant + MQTT           │
              │ Metrics + Logs                  │
              └──────────┬───────────┬──────────┘
                         │           │
                       MQTT      HA integrations
                         │           │
                         ▼           ▼
                    ESP room      Smart-home
                     nodes          devices
```

## 6. Home-server stack

- `hub-api`: device API, automatic registration, WebSocket sessions, status aggregation.
- Home Assistant: entities, integrations, scenes, automations.
- Mosquitto MQTT: custom room-node event/control bus.
- Speech-to-text: local Whisper/faster-whisper class service.
- Intent router: deterministic home commands + AI fallback.
- AI provider adapter: local and/or cloud model behind one interface.
- Text-to-speech: local Piper-class service or pluggable TTS.
- Metrics/logs: server and device telemetry.

**Security rule:** the ESP32 never stores a privileged Home Assistant token.

## 7. ESP32 ↔ server communication

Use a persistent WebSocket for real-time state, events, voice-session progress, and notifications. Use HTTPS/REST for setup and diagnostics.

```text
BOOT
 ↓
Initialize hardware
 ↓
Connect Wi-Fi
 ↓
Discover/configure home server
 ↓
Register automatically
 ↓
Open WebSocket
 ↓
Sync device + home state
 ↓
READY
 ↓
Reconnect automatically on failure
```

Example hello:

```json
{
  "type": "device.hello",
  "device_id": "hall-terminal-01",
  "firmware": "0.1.0",
  "capabilities": ["touch", "display", "microphone", "speaker", "imu", "rtc"]
}
```

## 8. Voice assistant architecture

### Phase A — push-to-talk

```text
User touches microphone
        ↓
ESP32 records mono PCM
        ↓
Audio → home server
        ↓
Speech-to-text
        ↓
Intent router
   ┌────┴─────┐
   │          │
Home intent   General query
   │          │
Home Assistant   AI/LLM
   └────┬─────┘
        ↓
Validated response/action
        ↓
Text-to-speech
        ↓
ESP32 speaker + screen
```

### Phase B — wake word

- Evaluate on-device wake word using ESP32-S3 speech tooling.
- Alternative: server-side local wake-word detector.
- Prefer on-device detection to reduce continuous audio traffic.

### Safety boundary

```text
LLM / parser
    ↓
Structured intent
    ↓
Policy + validation
    ↓
Approved Home Assistant service call
```

## 9. User interface

- Home: clock, connection, voice state, quick scenes.
- Server: CPU, RAM, disk, service health, network, uptime.
- Devices: rooms, lights, plugs, fans, sensors, scenes.
- Assistant: listening/thinking/speaking, transcription, response, action.
- Diagnostics: Wi-Fi RSSI, latency, firmware, memory, SD, audio, touch, IMU.

Use LVGL for the final UI.

## 10. Firmware architecture

```text
firmware/
├── platformio.ini
├── src/
│   ├── main.cpp
│   ├── app/
│   ├── audio/
│   ├── display/
│   ├── input/
│   ├── network/
│   ├── protocol/
│   ├── storage/
│   └── system/
├── include/
├── lib/
└── test/
```

## 11. Server architecture

```text
backend/
├── app/
│   ├── api/
│   ├── websocket/
│   ├── auth/
│   ├── devices/
│   ├── home/
│   ├── voice/
│   ├── ai/
│   ├── metrics/
│   └── config/
├── tests/
├── Dockerfile
└── compose.yaml
```

Provider interfaces:

```text
SpeechToTextProvider
TextToSpeechProvider
AIProvider
HomeAutomationProvider
MetricsProvider
```

## 12. Repository structure

```text
home-ai-hub/
├── AGENTS.md
├── README.md
├── PROJECT.md
├── docs/
│   ├── architecture.md
│   ├── hardware.md
│   ├── protocol.md
│   ├── voice.md
│   ├── security.md
│   ├── roadmap.md
│   └── decisions/
├── firmware/
├── backend/
├── home-assistant/
├── infrastructure/
├── hardware/
│   ├── schematics/
│   ├── pinouts/
│   └── enclosure/
└── scripts/
```

## 13. Security and privacy

- Keep the system local by default.
- Never port-forward the ESP32.
- Keep privileged tokens and AI API keys on the server.
- Give the ESP32 a device ID used for audit records, not authorization.
- Show a visible listening indicator.
- Stream audio only during an active voice session.
- Do not retain raw voice audio by default.

## 14. Delivery roadmap

1. Hardware qualification.
2. Professional firmware skeleton.
3. Home-server connection.
4. Server dashboard.
5. Home control.
6. Push-to-talk assistant.
7. Wake-word assistant.
8. Product polish: enclosure, battery, OTA, provisioning, crash recovery.

## 15. First engineering backlog

1. Convert the current serial test into a repository managed from CLI/Codex.
2. Download and pin official Waveshare demo/pin definitions.
3. Build a `board_diagnostics` firmware target.
4. Test PSRAM and flash.
5. Test display and touch.
6. Test ES8311 record/playback.
7. Test SD, IMU, RTC, and power management.
8. Create `hub-api`.
9. ESP32 calls `/health`.
10. Stream health over the direct WebSocket session.
11. Show server health on screen.
12. Add Home Assistant adapter.
13. Control one safe test entity.
14. Add push-to-talk audio.
15. Add STT → intent → TTS.
16. Add wake word.

## 16. Version 1 definition

Version 1 is complete when the ESP32 terminal boots, connects securely to the home server, shows live server/home status, controls approved devices from the touchscreen, accepts a push-to-talk spoken request, processes it on the server, performs a validated action or answers a question, and speaks/displays the response while recovering cleanly from Wi-Fi/server outages.

## 17. Reference baseline

- Waveshare: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5
- Home Assistant Assist pipelines: https://developers.home-assistant.io/docs/voice/pipelines/
