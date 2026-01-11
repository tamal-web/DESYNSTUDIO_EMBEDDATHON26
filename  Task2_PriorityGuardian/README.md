# Task 2 — Priority Guardian (Design, Implementation & Evidence)

## Overview

**Objective:** Maintain a continuous low-priority rolling-average calculation on numbers published to `krillparadise/data/stream` while immediately handling high-priority “CHALLENGE” messages on the TeamID topic. When a CHALLENGE arrives, the device must publish an ACK to `<ReefID>` within 250 ms (preferably <150 ms) and visually signal via LED during the event.

This README documents the architecture, concurrency strategy, timing measurement, and evidence format.

## Table of Contents

1. [Requirements & Success Criteria](#1-requirements--success-criteria)
2. [Hardware & Wiring](#2-hardware--wiring)
3. [Software & Dependencies](#3-software--dependencies)
4. [System Architecture & Task Priorities](#4-system-architecture--task-priorities)
5. [Engineering Highlights](#5-engineering-highlights-optimization--robustness)
6. [Function & API-level explanations](#6-function--api-level-explanations)
7. [Timing measurement & timestamp policy](#7-timing-measurement--timestamp-policy)
8. [Logging & Evidence format](#8-logging--evidence-format)
9. [Test plan](#9-test-plan)

---

## 1. Requirements & Success Criteria

- **Subscribe** to `krillparadise/data/stream` -> Compute rolling average (last 10).
- **Subscribe** to `<TeamID>` -> On "CHALLENGE" payload, immediately ACK and Flash LED.
- **Latency:** Must be ≤ 250 ms.
- **Concurrency:** Rolling average must continue (in background) without blocking the Distress response.

## 2. Hardware & Wiring

### Components
- **ESP32 DevKit V1**
- **Distress LED** (Built-in LED or external).
- **Serial Connection** for logs.

### Wiring
| Component | Pin | Note |
|-----------|-----|------|
| Distress LED | GPIO 25 | Active-Low (or configured in software) |

## 3. Software & Dependencies

- `PubSubClient`: MQTT.
- `FreeRTOS`: Task scheduling.

## 4. System Architecture & Task Priorities

The system uses a **Priority-Based Preemptive Scheduling** model with FreeRTOS:

1.  **Priority 3 (Highest) - `taskDistress`:**
    -   Waits on `qDistressSignals`.
    -   Preempts everything else when a signal arrives.
    -   Handles ACK publishing and LED toggling immediately.

2.  **Priority 2 (Medium) - `taskDispatcher`:**
    -   Runs the MQTT loop (`mqttClient.loop()`).
    -   Routes incoming messages to appropriate queues.
    -   Needs medium priority to keep the network alive but yield to the distress handler.

3.  **Priority 1 (Lowest) - `taskBackground`:**
    -   Waits on `qBackgroundValues`.
    -   Computes the mathematically intensive (simulated) rolling average.
    -   Runs only when high-priority tasks are idle.

## 5. Engineering Highlights: Optimization & Robustness

### Real-time (RTOS) Architecture & Innovation
We implemented a **"Three-Tier Priority Guardian"** pattern. This is innovative because it completely decouples the *receipt* of a message from its *processing*.
- The MQTT Dispatcher (Tier 2) acts as a high-throughput router. It does almost zero work (copy & push to queue).
- The Distress Handler (Tier 3) is a "sleeping giant." It consumes 0% CPU until a challenge arrives, at which point it instantaneously preempts the system to fire the ACK.

### Optimization
- **Queue Inter-Process Communication (IPC):** We transmit data between tasks using FreeRTOS Queues (`xQueueSend` / `xQueueReceive`). This allows the background task to be "fed" data at network speed but process it at its own pace (Backpressure).
- **Format String Optimization:** We use `snprintf` with pre-allocated character buffers for logging. This avoids the heap overhead and potential fragmentation associated with repeated `String` concatenations in tight loops.

### Resource Management
- **Backpressure Handling:** The `qBackgroundValues` has a finite length (16). If the Network Task receives data faster than the Background Task can compute averages, the `xQueueSend` uses a timeout (0ms), effectively dropping old packets rather than crashing the system (OOM). This is a standard *Load Shedding* technique in reliable distributed systems.
- **Stack Tuning:** Each task is pinned to Core 1 (App Core) with defined stack limits to ensure predictable memory footprint.

### Error Handling
- **Non-blocking Network Logic:** The `setup()` routine creates tasks immediately. If Wi-Fi fails, the tasks are running but idle. The Dispatcher handles reconnection internally with randomized backoff (`reconnectMQTT`), ensuring the device recovers from temporary router outages without a hard reset.
- **Data Integrity:** The dispatcher performs a basic `isDigit` check on background stream data before enqueuing, protecting the background task from parsing garbage and throwing exceptions.

## 6. Function & API-level explanations

### `setup()`
Initializes Serial, WiFi, and creates the three tasks: `PRIORITY_BACKGROUND=1`, `PRIORITY_DISPATCHER=2`, `PRIORITY_DISTRESS=3`.

### `mqttCallback(topic, payload, length)`
**Minimalist Design.**
- Copies payload -> Pushes to Queue.
- *Why:* Keeps the network stack unblocked.

### `taskDistress(pv)`
The critical path.
1. Unblocks immediately on `qDistressSignals`.
2. Turns LED ON.
3. Captures `now_ms()`.
4. Publishes ACK.
5. Turns LED OFF.
6. Logs delta time.

## 7. Timing measurement & timestamp policy

- **Source of Truth:** `millis()` (or `esp_timer_get_time()` for µs).
- **Measurement:** Start time is captured in `mqttCallback` or upon queue receipt. End time is captured immediately after `mqtt.publish`.
- **Constraint:** `delta_ms` must be < 250.

## 8. Logging & Evidence format

Serial logs are formatted for easy CSV conversion/parsing by judges:
```
[Background] msg=10.000 count=1 avg=10.00 time_ms=5000
[Dispatcher] Distress received at ms=5500 payload=CHALLENGE
[Distress] recv_ms=5500 ack_attempt_ms=5505 delta=5ms publish=OK
```

## 9. Test plan

1.  **Background Check:** Publish `10`, `20`, `30`. Verify Average prints `10`, `15`, `20`.
2.  **Distress Check:** Publish `CHALLENGE` to `<TeamID>`. Verify LED flash and low delta.
