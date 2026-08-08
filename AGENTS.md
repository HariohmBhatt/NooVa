# AGENTS.md

## Project

Firmware for the Waveshare ESP32-S3 Touch LCD 3.5.

## Framework

- Arduino
- ESP32 Arduino Core

## Coding Standards

- Modular architecture
- No code duplication
- No magic numbers
- Explain architectural decisions
- Keep compilation passing after every change

## Hardware

Board:
- ESP32-S3
- 16MB Flash
- 8MB PSRAM
- ST7796 LCD
- FT6336 Touch
- ES8311 Audio
- QMI8658 IMU
- PCF85063 RTC

## Rules

- Never invent GPIO pins.
- Always verify against the official Waveshare documentation.
- Prefer constexpr over macros.
- Avoid dynamic allocation unless justified.
- Keep functions under ~40 lines when practical.
- One class per file.
- Use RAII where appropriate.
- Document every public API.
- Commit all changes of the repository after a change has taken place with an explanatory commit message

## Workflow

When implementing a feature:

1. Explain architecture.
2. Explain affected files.
3. Implement.
4. Explain testing.
5. Wait for confirmation.

## Agent skills

### Issue tracker

Issues are tracked in GitHub Issues. See `docs/agents/issue-tracker.md`.

### Triage labels

The default canonical triage labels are used. See `docs/agents/triage-labels.md`.

### Domain docs

This repository uses a single-context domain-doc layout. See `docs/agents/domain.md`.
