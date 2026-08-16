# AGENTS.md

## Project

Firmware for the Waveshare ESP32-S3 Touch LCD 3.5.

## Framework

- Arduino
- ESP32 Arduino Core

## Repository boundaries

- `firmware/` contains device-side firmware and hardware-facing services.
- `backend/` contains the optional server-side hub and its tests.
- `hardware/` contains board-specific experiments and acceptance tests.
- `tools/` contains development and preview tooling; it must not become a firmware dependency.
- `docs/` contains protocol and architecture decisions that affect more than one module.
- Generated output, caches, local databases, and build artifacts do not belong in Git.

## Architecture direction

- Keep board and peripheral details behind small, testable interfaces.
- Keep application and domain logic independent of GPIO, display drivers, and transport details.
- Make dependencies point from composition and infrastructure toward stable application logic.
- Treat `firmware/hardware/BoardPins.h` and official Waveshare documentation as the pin-assignment source of truth.
- Prefer narrow modules with explicit ownership over feature logic concentrated in `main.cpp` or a controller class.
- Record cross-cutting architectural decisions in `docs/` before making them implicit in code.

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
4. Keep the change compiling after each coherent step.
5. Explain and run the relevant tests or hardware checks.
6. Commit the complete change with an explanatory message.
7. Wait for confirmation before starting unrelated work.
