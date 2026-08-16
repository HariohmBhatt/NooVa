# Goal
We want to build software for the hardware defined in 'README.md'. If you write any code, make sure to test it against the device connected. We want to make sure the code is hardware acceptable.

## Rules
1. Make sure you write code under: /src for ESP 32 /backend for server
2. Well document the code with comments. 
3. The CPU load of the software should be minimal on the ESP 32 device, and the maximum trace of deployed items should be on the server itself.
4. Prefer modules instead of singleton codes. Have scalable and tiny modules to reuse. 

## Server
We are building thru a server right now, which is the current device. I want you to make sure that when building the backend service that interacts in any way with the ESP 32 device, I want that to be deployed safely via a docker container that can be controlled and defined via a single docker compose instance. 

## Reference
For reference, you can look at the 'codex/simple-device-stats-wifi' branch for referencing code for the ESP32. 

## Coding style 
For all code that you write, make sure to have ample explanation of the flow in the code via comments. And prefer simple solutions over any complex ones. 

## How we work - specifically for ESP32

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