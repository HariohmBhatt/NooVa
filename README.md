# NooVa Firmware

NooVa is firmware for the **Waveshare ESP32-S3 Touch LCD 3.5**. This repository is being rebuilt from a deliberately blank foundation so the hardware boundaries, module structure, and development workflow are clear from the beginning.

The previous implementation is preserved on the [archive branch](https://github.com/HariohmBhatt/NooVa/tree/archive/simple-device-stats-wifi-2026-08-16).

## The machine we are building for

| Part | Hardware |
| --- | --- |
| Development board | Waveshare ESP32-S3 Touch LCD 3.5 |
| Microcontroller | Espressif ESP32-S3 |
| Flash | 16 MB |
| PSRAM | 8 MB |
| Display | ST7796 LCD |
| Touch controller | FT6336 |
| Audio codec | ES8311 |
| Inertial sensor | QMI8658 IMU |
| Real-time clock | PCF85063 RTC |

The board is an embedded device with a display, touch input, audio, motion sensing, and timekeeping. Firmware must therefore be conscious of limited resources, hardware timing, startup order, and failures that do not exist in ordinary desktop applications.

## Software stack

- Arduino framework
- ESP32 Arduino Core
- C++17
- PlatformIO for repeatable builds once the project configuration is established

Hardware-independent logic should be testable on the host wherever practical. Hardware-facing code should remain small and isolated so it can be checked against the real board.

## Hardware rules

Hardware documentation is the source of truth.

- Never guess a GPIO, bus assignment, polarity, or peripheral address.
- Verify board details against the official Waveshare documentation and schematics before implementing drivers.
- Keep board-specific definitions in one dedicated place when the firmware structure is introduced.
- Test changes on the physical board when they affect display, touch, audio, sensors, storage, power, or timing.
- Do not commit credentials, private keys, generated certificates, or other secrets.

## How we work

We are aiming for maintainable embedded software rather than a large program concentrated in `main.cpp`.

1. Describe the behavior and the proposed architecture before changing code.
2. Identify the affected files and the hardware assumptions.
3. Keep hardware access behind narrow interfaces and keep application logic independent of GPIO and driver details.
4. Prefer `constexpr` values and named types over magic numbers. Avoid dynamic allocation unless there is a clear reason.
5. Keep functions small, document public APIs, and avoid duplicating logic.
6. Keep the project compiling after every coherent change.
7. Run the relevant host tests, static checks, build, and hardware checks.
8. Commit one coherent change with an explanatory message, then push it to the working branch.

Architectural decisions that affect multiple modules should be recorded in `docs/` so they do not live only in conversation or memory.

## Starting from the blank canvas

The active branch is [`refactor/clean-foundation`](https://github.com/HariohmBhatt/NooVa/tree/refactor/clean-foundation). A new contributor can begin with:

```bash
git clone git@github.com:HariohmBhatt/NooVa.git
cd NooVa
git fetch origin
git switch --track origin/refactor/clean-foundation
```

The first implementation work should establish the project layout, build configuration, and verified board definitions before adding application features.

## Contributions

Before opening a pull request:

- confirm the change is on the intended branch;
- explain any hardware assumptions;
- include the commands and hardware checks used for validation;
- keep generated files and local runtime data out of commits; and
- use a commit message that explains the change.
