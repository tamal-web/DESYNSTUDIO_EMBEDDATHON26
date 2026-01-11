# Task 3 — Window Synchronizer (Design, Implementation & Evidence)

## Overview

**Objective:** Detect a short "window open" event broadcast by the broker (<window_code> topic), and physically press a hardware push-button during that window with ±50 ms tolerance. Implement debouncing and reliably publish `{"status":"synced", ...}` when synchronized.

## Table of Contents

1. [Requirements & Success Criteria](#1-requirements--success-criteria)
2. [Hardware & Wiring](#2-hardware--wiring)
3. [Software & Dependencies](#3-software--dependencies)
4. [System Architecture](#4-system-architecture)
5. [Engineering Highlights](#5-engineering-highlights-optimization--robustness)
6. [Function-level description](#6-function-level-description)
7. [Timing & debouncing strategy](#7-timing--debouncing-strategy)
8. [Test plan](#8-test-plan)

---

## 1. Requirements & Success Criteria

- Listen for `{"status":"open"}` on `<window_code>`.
- Detect physical button press.
- **Sync Criterion:** `abs(press_time - window_open_time) <= 50ms` OR `press_time` within window duration.
- **Debounce:** ≥ 20ms.
- **Output:** Publish `{"status":"synced"}`.

## 2. Hardware & Wiring

### Components
- **ESP32 DevKit V1**
- **Push Button** (Momentary)
- **LEDs:** Blue (Window), Green (Success/Press), Red (Idle).

### Wiring
| Component | Pin | Note |
|-----------|-----|------|
| Button | GPIO 15 | Input Pull-up |
| Blue LED | GPIO 24 | Window Open Indicator |
| Green LED | GPIO 26 | Press Indicator |
| Red LED | GPIO 25 | Idle Indicator |

*Logic:* LEDs are Active-Low (LOW = ON).

## 3. Software & Dependencies

- `PubSubClient` (MQTT)
- `ArduinoJson`
- Standard Arduino Core

## 4. System Architecture

The implementation uses a **Super-Loop Polling Architecture** (Single Threaded). Unlike complex multi-tasking solutions, this approach minimizes overhead to ensure the loop runs fast enough (typically <1ms per iteration) to capture button presses accurately.

- **MQTT Client (`mqtt.loop`)**: Polls network.
- **Button Handler (`handleButtonPress`)**: Polls GPIO state.
- **Sync Logic (`checkWindowSync`)**: Compares timestamps.

## 5. Engineering Highlights: Optimization & Robustness

### Real-time Architecture (Super Loop)
While Task 1 and 2 uses FreeRTOS, Task 3 intentionally uses a **Bare Metal Super Loop**. Why?
- **Zero Overhead:** Context switching and task notification take microseconds. A polling loop checking a GPIO register takes nanoseconds.
- **Rationale for Task 3:** Detecting a button press within a *milliseconds* window is best done by checking the pin as frequently as possible. By stripping away OS complexity, we maximize the sampling rate of the button pin.

### Optimization
- **Non-blocking Timers:** We strictly use `millis()` checks for debouncing and LED flashing. There are **zero** `delay()` calls in the main loop (only `delay(1)` at the end to satisfy the watchdog). This ensures the processor is always available to detect an incoming MQTT packet or a button press.
- **Immediate Interrupt Logic:** The `mqtt_callback` sets the state variable `windowOpen = true` immediately. It does not perform JSON serialization or heavy logging *before* setting the flag. This brings the software-defined "Window Start" as close to the network packet arrival as possible.

### Resource Management
- **Stack Reuse:** By using a single thread, the ESP32 reuses the same stack for all operations, maximizing available heap for the MQTT buffer.
- **Volatile Variables:** Shared state flags (`windowOpen`, `buttonPressed`) are declared `volatile`, inhibiting compiler optimization that might cache the values in registers, thus ensuring the main loop always sees the latest logic updates.

### Error Handling
- **State Cleanup:** The `checkWindowSync` function includes a timeout logic (`currentTime - windowOpenTime > 1500`). This ensures that if a window packet is received but no button is pressed, the system self-heals (resets to IDLE) rather than getting stuck in an "Open" state forever.
- **JSON Safety:** We check `error` code from `deserializeJson`. If a malformed packet arrives, it is safely ignored, preventing the parsing engine from crashing.

## 6. Function-level description

### `mqtt_callback`
Sets `windowOpen = true` and records `windowOpenTime = millis()` immediately.

### `handleButtonPress`
Reads `digitalRead(BUTTON_PIN)`.
- **Debouncing:** Ignores changes if `millis() - lastDebounceTime < 20`.
- **Detection:** On falling edge, sets `buttonPressed = true`.

### `checkWindowSync`
Runs in main loop. Checks if `buttonPressed` occurred *after* `windowOpenTime`. If valid, computes delta and calls `publishSync`.

## 7. Timing & debouncing strategy

- **Clock:** `millis()` is used for all timestamps.
- **Synchronization Logic:**
  Successful if: `buttonPressed && (pressTime >= windowOpenTime)`

## 8. Test plan

1. **Trigger Window:** Send `{"status":"open"}`.
2. **Action:** Press button immediately.
3. **Result:** Green LED flashes. Console shows "SUCCESS". Blue turns OFF.
