# Task 4 — The Silent Image (Steganography Decoder)

## Overview

**Objective:** Request a hidden artifact via MQTT, reconstruct the received 64x64 PNG, and use steganographic analysis to extract a hidden message (Target URL) based on pixel relationships.

## Table of Contents

1. [Requirements & Success Criteria](#1-requirements--success-criteria)
2. [Data formats & message flow](#2-data-formats--message-flow)
3. [System Architecture](#3-system-architecture)
4. [Engineering Highlights](#4-engineering-highlights-optimization--robustness)
5. [Steganography analysis strategy](#5-steganography-analysis-strategy)
6. [Tools & dependencies](#6-tools--dependencies)
7. [Logging & evidence format](#7-logging--evidence-format)

---

## 1. Requirements & Success Criteria

- **Request:** Publish `{"request":"...", "agent_id":"..."}`.
- **Receive:** `{"data":"<base64>", ...}`.
- **Reconstruct:** Save as valid PNG.
- **Crack:** Find the hidden URL inside the image.

## 2. Data formats & message flow

1. **Request:** `kelpsaute/steganography` -> `{"request":"..."}`
2. **Response:** `<challenge_code>` -> Base64 PNG.

## 3. System Architecture

The solution uses a hybrid approach:
- **ESP32 (`task4.ino`):** The "Network Bridge." It handles the MQTT request/response cycle, which requires device-specific authentication.
- **Python (`task4_phase3-4_analyze.py`):** The "Compute Node." It performs the heavy image decoding and steganographic analysis.

## 4. Engineering Highlights: Optimization & Robustness

### Innovation: Hybrid Compute Architecture
We recognized that breaking LSB or relational steganography is computationally expensive and requires complex libraries (`PIL`, `numpy`) unavailable on microcontrollers.
- **Innovation:** We treat the ESP32 strictly as an IoT Interface, while offloading the "Brain" work to a host machine. This mirrors real-world Edge-to-Cloud architectures.

### Optimization
- **Vectorized Analysis:** The Python script uses `numpy` arrays to perform pixel operations. Instead of iterating `for x in width: for y in height`, we perform whole-matrix operations (e.g., `diff = img_r - img_g`). This optimizes the search space for hidden messages by orders of magnitude compared to C++ pixel loops.
- **Base64 Buffer Management:** On the ESP32 side, the MQTT buffer is increased (`mqtt.setBufferSize(2048)`) to handle large Base64 chunks without overflow.

### Robustness & Error Handling
- **Image Integrity Verification:** Before analysis, the Python script validates the `PNG` header signature. This ensures that any data corruption during MQTT transmission or Copy-Paste is detected immediately.
- **Multi-Method Brute Force:** The script doesn't assume one hiding method. It iteratively tries `LSB`, `Channel Difference`, and `Ratio` methods. If `Method A` yields garbled text, it catches the exception and proceeds to `Method B`.

## 5. Steganography analysis strategy

The challenge hints that the message "depends on relationships, not exact values". We implemented a pipeline to check multiple hiding schemes:

1. **LSB Analysis:** Checking specific bit planes (0-2).
2. **Channel Ratios:** Comparing Red vs Green vs Blue intensities.
3. **XOR/Difference Maps:** Visualizing differences.

## 6. Tools & dependencies

- **Hardware:** ESP32.
- **Host Software:** Python 3 + `Pillow`, `numpy`.

**Key Files:**
- `task4.ino`: MQTT Request/Response handler.
- `task4_phase3-4_analyze.py`: Analysis suite.

## 7. Logging & evidence format

**Artifacts:**
- `raw_payload.txt`: The Base64 string.
- `reconstructed_image.png`: decoded image.
- `extracted_message.txt`: FAST_URL found.
