# Task 1 — Timing Keeper (Design, Implementation & Evidence)

## Overview

**Objective:** Read timing arrays for Red/Green/Blue channels from MQTT topic `shrimphub/led/timing/set` and reproduce the illumination pattern on an RGB LED with strict timing accuracy (±5 ms), no drift over 5 minutes. Provide reproducible artifacts and video proof with stopwatch and serial logs.

This README documents the hardware, software, architecture, code functions, testing procedures, logging format, troubleshooting, and final evidence.

## Table of Contents

1. [Requirements & Success Criteria](#1-requirements--success-criteria)
2. [Hardware & Wiring](#2-hardware--wiring)
3. [Software & Dependencies](#3-software--dependencies)
4. [System Architecture](#4-system-architecture)
5. [Code Walkthrough](#5-code-walkthrough--function-by-function)
6. [Engineering Highlights](#6-engineering-highlights-optimization--robustness)
7. [Timing strategy & precision considerations](#7-timing-strategy--precision-considerations)
8. [Logging & Evidence format](#8-logging--evidence-format)
9. [Test & Validation procedures](#9-test--validation-procedures)
10. [Failure modes & mitigations](#10-failure-modes--mitigations)
11. [How to reproduce / Run instructions](#11-how-to-reproduce--run-instructions)

---

## 1. Requirements & Success Criteria

- **Subscribe** to `shrimphub/led/timing/set`.
- **Parse JSON payload**:
  ```json
  {
    "red": [500, 200, 300, ...],
    "green": [400, 300, 200, ...],
    "blue": [600, 100, 400, ...]
  }
  ```
- **Implement on/off cycles** with durations in milliseconds using strict timing.
- **Repeat pattern continuously** until replaced by a new array.
- **Program must not drift** over 5 minutes.
- **Provide video**: stopwatch visible + serial log + LED visible.

## 2. Hardware & Wiring

### Components
- **ESP32 DevKit V1**
- **RGB LED** (Common Anode/Cathode)
- **OLED Display** (SSD1306, I2C) [Optional but used]
- **Resistors/MOSFETs** as needed.

### Wiring
| Component | ESP32 Pin |
|-----------|-----------|
| **RED_LED** | GPIO 25 |
| **GREEN_LED**| GPIO 26 |
| **BLUE_LED** | GPIO 27 |
| **SDA** (OLED)| GPIO 21 |
| **SCL** (OLED)| GPIO 22 |

*Note: The code logic assumes Active-Low LEDs (LOW = ON, HIGH = OFF) as per `task1.ino`.*

## 3. Software & Dependencies

### Toolchain
- **Arduino IDE** or **PlatformIO** with ESP32 board support.

### Libraries
- `ArduinoJson` (v6+) - for parsing MQTT payloads.
- `PubSubClient` - for MQTT communication.
- `Adafruit_GFX` & `Adafruit_SSD1306` - for OLED display.
- `FreeRTOS` (Built-in to ESP32 Arduino Core).

### Configurations
- `configTICK_RATE_HZ` is typically 1000 (1ms) on ESP32 Arduino, allowing `pdMS_TO_TICKS` to be accurate.

## 4. System Architecture

The solution uses **FreeRTOS** to separate network handling, display updates, and precise LED timing.

- **MQTT Task (network):** Maintains connection and handles incoming messages.
- **Display Task (low priority):** Updates the OLED with status (red/green/blue counts, time elapsed).
- **Three LED Tasks (high priority):** Independent tasks for Red, Green, and Blue channels. They wait for notifications and execute timing loops using `vTaskDelayUntil` for drift-free precision.
- **Semaphores:** Mutexes (`redSemaphore`, `greenSemaphore`, `blueSemaphore`) protect access to the shared timing arrays.

### Why separate tasks?
- **Isolation:** Network delays (in `MQTTTask`) do not affect LED timing.
- **Independence:** Each LED channel can run a different pattern length without complex state machines in a single loop.
- **Precision:** `vTaskDelayUntil` ensures the wake time is calculated from the *previous* wake time, preventing cumulative error.

## 5. Code Walkthrough — function-by-function

### `setup()`
Initializes Serial, WiFi connection, MQTT settings, Semaphores, and creates the FreeRTOS tasks.
*Why:* Central initialization ensures all resources are ready before tasks start.

### `mqttCallback(topic, payload, length)`
Receives the JSON payload.
1. Parses JSON using `ArduinoJson`.
2. Acquires Mutexes (`xSemaphoreTake`).
3. Updates global `redTimings`, `greenTimings`, `blueTimings` arrays.
4. Releases Mutexes.
5. Notifies LED tasks (`xTaskNotifyGive`) to restart their patterns.
*Why:* Handles the data ingestion.

### `RedLEDTask`, `GreenLEDTask`, `BlueLEDTask`
The core timing engines.
1. `ulTaskNotifyTake`: Waits for the first pattern to arrive.
2. `xSemaphoreTake`: Copies the global timing array to a local buffer.
3. **Loop:** Toggles LED and uses `vTaskDelayUntil(&xLastWakeTime, ...)` for precise duration. Checks for task notifications (new pattern) to reset immediately.
*Why:* Local buffering prevents contention. `vTaskDelayUntil` guarantees zero drift.

## 6. Engineering Highlights: Optimization & Robustness

### Real-time (RTOS) Architecture
The system employs a **Preemptive Multitasking** model. The LED tasks are assigned higher priorities (3) than the Display task (2) and System/Network tasks. This ensures that even if the Display task takes 50ms to render the OLED, or the MQTT task is processing a packet, the LED toggle will occur exactly on the millisecond tick.
- **Innovation:** We utilize **Task Notifications** (`xTaskNotifyGive`/`ulTaskNotifyTake`) instead of heavy binary semaphores for signaling new patterns, reducing context switch overhead.

### Optimization
- **Drift Elimination:** We strictly use `vTaskDelayUntil` rather than `vTaskDelay`. The former calculates the next wake time based on the *scheduled* previous wake time, not the *actual* wake time. This mathematically eliminates cumulative error (drift) over long durations (e.g., 5-minute requirement).
- **Local Buffering:** Each LED task copies the global timing array into a local stack-allocated array (`int timings[50]`). This reduces the critical section time (Mutex hold time) to microseconds, effectively eliminating lock contention between the parser and the blinking tasks.

### Resource Management
- **Static JSON Allocation:** We use `StaticJsonDocument<2048>` instead of Dynamic. This allocates memory on the stack (or global static segment), preventing heap fragmentation which is a common cause of crash in long-running ESP32 firmwares.
- **Fixed Stack Sizes:** Tasks are allocated 4096 bytes of stack, tuned to accommodate `printf` formatting and recursion depth without wasting RAM.

### Error Handling & Robustness
- **Mutex Protection:** All shared data access is guarded by `xSemaphoreTake`. If the parser is writing, the reader waits. This prevents "tearing" where an LED task reads half of an old pattern and half of a new one.
- **Network Resilience:** The `MQTTTask` runs an infinite loop with connection checking (`if (!client.connected()) reconnect()`). If WiFi drops, the LED tasks **continue running** the current pattern uninterrupted, satisfying the robustness requirement.
- **Input Validation:** The callback checks `length` before copying and verifies `deserializeJson` return codes before processing.

## 7. Timing strategy & precision considerations

- **Drift Prevention:** `vTaskDelayUntil()` keeps the timeline absolute.
- **Task Priorities:** LED tasks (3) > Display (2).
- **Atomic Updates:** Using Semaphores ensures data integrity.

## 8. Logging & Evidence format

Serial output provides authoritative logs.

**Format:**
```
topic: shrimphub/led/timing/set
red: [500, 200, ...]
✓ Timings Updated!
```

## 9. Test & Validation procedures

1. **Unit Test:** Send `{"red":[1000,1000]}`. Verify Red LED blinks 1s ON, 1s OFF.
2. **Drift Test:** Run a pattern for 5 minutes. Compare the 300th blink with the stopwatch.
3. **Update Test:** Send a new JSON while the old one is running. Verify instantaneous switch.

## 10. Failure modes & mitigations

- **WiFi Disconnect:** `MQTTTask` handles reconnection. LED tasks continue running the *last known* pattern.
- **Priority Inversion:** Mutex critical sections are extremely short (memcpy only) to prevent blocking high-priority LED tasks.

## 11. How to reproduce / Run instructions

1. Open `task1.ino` in Arduino IDE.
2. Update `ssid`, `password`, and `mqtt_server`.
3. Upload to ESP32.
4. Send MQTT payload to `shrimphub/led/timing/set`.
5. Observe LED and serial logs.
