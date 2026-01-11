// filename: priority_guardian.ino
// Complete Task 2 implementation: Background rolling average + prioritized
// distress ACK within timing constraints Platform: ESP32 (Arduino core). Uses
// FreeRTOS tasks, WiFi, and MQTT. Security note: Do NOT hardcode real
// credentials when committing publicly. Use placeholders and a config file in
// production.

// ===================== Includes & Config =====================
#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// --------------------- User Configuration ---------------------
// Replace the placeholder values below with your actual setup via a local,
// untracked config.h if possible.
#ifndef WIFI_SSID
#define WIFI_SSID "Airtel_Shri Radhey"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "BankeyBihari@152"
#endif

// If broker uses TLS, set BROKER_SSL to true and provide certificates or
// disable verification only in testing.
#ifndef BROKER_HOST
#define BROKER_HOST                                                            \
  "broker.mqttdashboard.com" // e.g., "broker.example.com" or IP like
                             // "192.168.1.100"
#endif
#ifndef BROKER_PORT
#define BROKER_PORT 1883 // 8883 for TLS
#endif
#ifndef BROKER_SSL
#define BROKER_SSL false // true if using TLS (then use WiFiClientSecure)
#endif

// Team identifiers
#ifndef TEAM_ID
#define TEAM_ID "tamaldesyn" // e.g., "gajendraoceans11"
#endif
#ifndef REEF_ID
#define REEF_ID "tamalkchhabra2007desyn" // e.g., "sherkoceans11"
#endif

// Topics
#define TOPIC_BACKGROUND "krillparadise/data/stream"
#define TOPIC_DISTRESS TEAM_ID // Random “CHALLENGE” events arrive here
#define TOPIC_ACK REEF_ID      // Publish ACKs to this topic
// Note: Next challenge codes will be published after 10 successful distress
// ACKs; we will log any extra broker messages.

// GPIO Pins (adjust per your board wiring)
#define LED_DISTRESS_PIN                                                       \
  25 // Built-in LED on many ESP32 dev boards is often GPIO2; change as needed

// FreeRTOS Task Priorities (higher number = higher priority on ESP32 Arduino)
#define PRIORITY_BACKGROUND 1
#define PRIORITY_DISPATCHER 2
#define PRIORITY_DISTRESS 3

// Queue sizes
#define BACKGROUND_QUEUE_LEN 16
#define DISTRESS_QUEUE_LEN 8

// Rolling average parameters
#define ROLLING_WINDOW 10

// Timing constraints
#define ACK_HARD_LIMIT_MS 250 // Must be <= 250 ms
#define ACK_TARGET_MS 150     // Target for evaluation
#define LED_ON_ON_RECEIVE true

// MQTT reconnect parameters
#define MQTT_RETRY_DELAY_MS 2000
#define WIFI_RETRY_DELAY_MS 2000

// ===================== Globals =====================
WiFiClient client;
WiFiClientSecure secureClient;
PubSubClient mqttClient(BROKER_SSL ? (Client &)secureClient : (Client &)client);

TaskHandle_t taskBackgroundHandle = nullptr;
TaskHandle_t taskDispatcherHandle = nullptr;
TaskHandle_t taskDistressHandle = nullptr;

// Queues
QueueHandle_t qBackgroundValues =
    nullptr; // queue of float values for rolling average
QueueHandle_t qDistressSignals =
    nullptr; // queue of uint32_t timestamps when distress received
SemaphoreHandle_t mqttMutex = nullptr; // Mutex for MQTT client protection

// Success counter for distress acknowledgments
volatile uint32_t distressAckCount = 0;

// For logging: millisecond clock base
static inline uint32_t now_ms() { return (uint32_t)millis(); }

// ===================== Utility: Safe Logging =====================
void logLine(const String &msg) {
  // Single function to print logs; helpful for consistent format in video
  // evidence.
  Serial.println(msg);
}

// ===================== WiFi & MQTT =====================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  logLine("[Net] Connecting to WiFi...");
  uint32_t start = now_ms();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    if ((now_ms() - start) > 30000) {
      logLine("\n[Net] WiFi connect timeout; retrying...");
      start = now_ms();
      WiFi.disconnect();
      delay(WIFI_RETRY_DELAY_MS);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }
  logLine("\n[Net] WiFi connected: " + WiFi.localIP().toString());
}

void configureTLSIfNeeded() {
  if (BROKER_SSL) {
    // SECURITY: In production, validate broker certificate instead of insecure
    // mode. For demo/testing only:
    secureClient.setInsecure();
    logLine("[Net] TLS enabled (setInsecure for demo). Provide proper certs "
            "for production.");
  }
}

bool mqttReconnect() {
  while (true) {
    xSemaphoreTake(mqttMutex, portMAX_DELAY);
    bool isConnected = mqttClient.connected();
    xSemaphoreGive(mqttMutex);

    if (isConnected)
      return true;

    String clientId =
        String("ESP32-") + String((uint32_t)ESP.getEfuseMac(), HEX);
    logLine("[MQTT] Connecting as " + clientId + " to " + String(BROKER_HOST) +
            ":" + String(BROKER_PORT));

    xSemaphoreTake(mqttMutex, portMAX_DELAY);
    if (mqttClient.connect(clientId.c_str())) {
      logLine("[MQTT] Connected");

      // Subscribe to required topics
      if (mqttClient.subscribe(TOPIC_BACKGROUND)) {
        logLine(String("[MQTT] Subscribed: ") + TOPIC_BACKGROUND);
      } else {
        logLine(String("[MQTT] Failed to subscribe: ") + TOPIC_BACKGROUND);
      }
      if (mqttClient.subscribe(TOPIC_DISTRESS)) {
        logLine(String("[MQTT] Subscribed: ") + TOPIC_DISTRESS);
      } else {
        logLine(String("[MQTT] Failed to subscribe: ") + TOPIC_DISTRESS);
      }
      xSemaphoreGive(mqttMutex);
      return true;
    } else {
      int state = mqttClient.state();
      xSemaphoreGive(mqttMutex);
      logLine(String("[MQTT] Connect failed (rc=") + state + "), retrying in " +
              String(MQTT_RETRY_DELAY_MS) + " ms");
      delay(MQTT_RETRY_DELAY_MS);
    }
  }
}

// ===================== MQTT Callback =====================
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  // Keep callback minimal: copy payload, parse, and post to queues.
  String t = String(topic);
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  // Background stream: float values
  if (t == TOPIC_BACKGROUND) {
    // Attempt to parse as float
    float value = NAN;
    // Trim spaces
    msg.trim();
    // Some brokers may send plain floats like "23.5" or JSON; handle simple
    // plain floats first.
    value = msg.toFloat(); // if non-numeric, toFloat returns 0.0; we’ll guard
                           // with isDigit checks.
    bool looksNumeric = (msg.length() > 0) &&
                        (isDigit(msg[0]) || msg[0] == '-' || msg[0] == '+');
    if (!looksNumeric && msg[0] != '0') {
      logLine(String("[Dispatcher] Ignored non-numeric background payload: ") +
              msg);
      return;
    }
    if (xQueueSend(qBackgroundValues, &value, 0) != pdTRUE) {
      logLine("[Dispatcher] Background queue full, dropping value");
    }
    return;
  }

  // Distress: expects "CHALLENGE"
  if (t == TOPIC_DISTRESS) {
    // Minimal parse: detect keyword "CHALLENGE"
    bool isChallenge = (msg.indexOf("CHALLENGE") != -1);
    if (isChallenge) {
      uint32_t ts = now_ms();
      if (xQueueSend(qDistressSignals, &ts, 0) != pdTRUE) {
        logLine("[Dispatcher] Distress queue full, dropping signal");
      } else {
        if (LED_ON_ON_RECEIVE) {
          digitalWrite(LED_DISTRESS_PIN, LOW); // LED ON at receive
        }
        logLine(String("[Dispatcher] Distress received at ms=") + ts +
                " payload=" + msg);
      }
    } else {
      logLine(String("[Dispatcher] Non-challenge distress topic message: ") +
              msg);
    }
    return;
  }

  // Unknown topics: log for completeness (broker may publish next challenge
  // codes)
  logLine(String("[Dispatcher] Unhandled topic '") + t + "' payload: " + msg);
}

// ===================== Tasks =====================

// Priority 1: Background rolling average task
void taskBackground(void *pv) {
  // Rolling buffer and running sum for last 10 values
  float buffer[ROLLING_WINDOW];
  uint8_t count = 0;
  uint8_t head = 0;
  double runningSum = 0.0;

  for (;;) {
    float incoming = NAN;
    // Wait for next value; block up to 500 ms to avoid busy loop
    if (xQueueReceive(qBackgroundValues, &incoming, pdMS_TO_TICKS(500)) ==
        pdTRUE) {
      // Insert into ring buffer
      if (count < ROLLING_WINDOW) {
        buffer[head] = incoming;
        runningSum += incoming;
        count++;
      } else {
        // Replace oldest
        float old = buffer[head];
        runningSum -= old;
        buffer[head] = incoming;
        runningSum += incoming;
      }
      head = (head + 1) % ROLLING_WINDOW;

      double avg = runningSum / (double)count;
      // Print rolling average with 2 decimals
      char out[96];
      snprintf(out, sizeof(out),
               "[Background] msg=%.3f count=%u avg=%.2f time_ms=%u", incoming,
               count, avg, now_ms());
      logLine(String(out));
    } else {
      // Periodic heartbeat to show task alive without spamming
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

// Priority 3: Distress handler (ACK path)
void taskDistress(void *pv) {
  for (;;) {
    uint32_t tsReceive = 0;
    // Block waiting for distress signals
    if (xQueueReceive(qDistressSignals, &tsReceive, portMAX_DELAY) == pdTRUE) {
      // Immediately publish ACK; must be <= 250 ms from receive
      uint32_t tsAckAttempt = now_ms();
      uint32_t delta =
          (tsAckAttempt >= tsReceive) ? (tsAckAttempt - tsReceive) : 0;

      // Build ACK payload
      // {"status": "ACK", "timestamp_ms": <your_current_millis>}
      String payload = String("{\"status\":\"ACK\",\"timestamp_ms\":") +
                       String(tsAckAttempt) + String("}");

      bool published = false;
      if (xSemaphoreTake(mqttMutex, portMAX_DELAY) == pdTRUE) {
        published = mqttClient.publish(TOPIC_ACK, payload.c_str());
        xSemaphoreGive(mqttMutex);
      }
      uint32_t tsAckDone = now_ms();
      uint32_t deltaDone =
          (tsAckDone >= tsReceive) ? (tsAckDone - tsReceive) : 0;

      // LED OFF after ACK
      digitalWrite(LED_DISTRESS_PIN, HIGH);

      // Logging
      char out[160];
      snprintf(out, sizeof(out),
               "[Distress] recv_ms=%u ack_attempt_ms=%u ack_done_ms=%u "
               "delta_attempt=%u ms delta_done=%u ms (limit<=%u, target<=%u) "
               "publish=%s",
               tsReceive, tsAckAttempt, tsAckDone, delta, deltaDone,
               ACK_HARD_LIMIT_MS, ACK_TARGET_MS, published ? "OK" : "FAIL");
      logLine(String(out));

      if (published && deltaDone <= ACK_HARD_LIMIT_MS) {
        distressAckCount++;
        logLine(String("[Distress] ACK success count = ") + distressAckCount);
        // After 10 successful acks, broker will publish next challenge; we keep
        // listening and logging.
      } else {
        logLine(
            "[Distress] WARNING: ACK timing exceeded limit or publish failed");
      }
    }
  }
}

// Priority 2: MQTT dispatcher task (maintain connection and process loop)
void taskDispatcher(void *pv) {
  // Configure MQTT
  mqttClient.setServer(BROKER_HOST, BROKER_PORT);
  mqttClient.setCallback(mqttCallback);

  for (;;) {
    xSemaphoreTake(mqttMutex, portMAX_DELAY);
    bool isConnected = mqttClient.connected();
    xSemaphoreGive(mqttMutex);

    if (!isConnected) {
      mqttReconnect();
    }

    xSemaphoreTake(mqttMutex, portMAX_DELAY);
    mqttClient.loop(); // process incoming messages; callback runs here
    xSemaphoreGive(mqttMutex);

    vTaskDelay(pdMS_TO_TICKS(5)); // short yield to let other tasks run
  }
}

// ===================== Setup & Loop =====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  logLine("[System] Task 2: Priority Guardian starting...");

  // Init LED pin
  pinMode(LED_DISTRESS_PIN, OUTPUT);
  digitalWrite(LED_DISTRESS_PIN, HIGH);

  // WiFi & MQTT
  connectWiFi();
  configureTLSIfNeeded();

  // Create queues
  mqttMutex = xSemaphoreCreateMutex();
  qBackgroundValues = xQueueCreate(BACKGROUND_QUEUE_LEN, sizeof(float));
  qDistressSignals = xQueueCreate(DISTRESS_QUEUE_LEN, sizeof(uint32_t));
  if (!qBackgroundValues || !qDistressSignals || !mqttMutex) {
    logLine("[System] ERROR: Queue creation failed");
    while (true) {
      delay(1000);
    }
  }

  // Create tasks with specified priorities
  BaseType_t ok1 =
      xTaskCreatePinnedToCore(taskBackground, "TaskBackground", 4096, nullptr,
                              PRIORITY_BACKGROUND, &taskBackgroundHandle, 1);
  BaseType_t ok2 =
      xTaskCreatePinnedToCore(taskDispatcher, "TaskDispatcher", 4096, nullptr,
                              PRIORITY_DISPATCHER, &taskDispatcherHandle, 1);
  BaseType_t ok3 =
      xTaskCreatePinnedToCore(taskDistress, "TaskDistress", 4096, nullptr,
                              PRIORITY_DISTRESS, &taskDistressHandle, 1);

  if (ok1 != pdPASS || ok2 != pdPASS || ok3 != pdPASS) {
    logLine("[System] ERROR: Task creation failed");
    while (true) {
      delay(1000);
    }
  }

  logLine(
      "[System] Tasks launched: Background(P1), Dispatcher(P2), Distress(P3)");
  logLine(String("[System] Subscribing to topics: ") + TOPIC_BACKGROUND + ", " +
          TOPIC_DISTRESS);
  logLine(String("[System] ACKs will be published to: ") + TOPIC_ACK);
  logLine("[System] Ready. Show stopwatch in video; watch serial logs for "
          "timing proof.");
}

void loop() {
  // Empty; all work in FreeRTOS tasks
  vTaskDelay(pdMS_TO_TICKS(1000));
}

/*
Deliverables covered by this code:
- Three FreeRTOS tasks with explicit priorities:
  - Priority 1: Background rolling average (queue-driven, non-blocking).
  - Priority 2: MQTT dispatcher (subscriptions, minimal callback, routing).
  - Priority 3: Distress handler (ACK publish, LED ON/OFF, timing evidence).
- MQTT subscriptions: krillparadise/data/stream (floats) and <TeamID>
(distress).
- ACK publish to <ReefID> with {"status": "ACK", "timestamp_ms": <ms>} within
≤250 ms.
- Visual indicator: Distress LED ON at receive, OFF at ACK publish.
- Robust serial logging:
  - Background: new value, count, average, timestamp.
  - Distress: receive time, ack attempt/done times, deltas, success counter.
- Resilience:
  - Non-blocking dispatcher loop.
  - Queue backpressure handling with logs.
  - WiFi/MQTT reconnect logic.
Security notes:
- Do not commit real WiFi credentials or broker secrets. Use placeholders or
external config.h (ignored by VCS).
- For TLS brokers, validate certificates (avoid setInsecure in production).

Repository artifacts suggestion:
- Place this file under Task2_PriorityGuardian/priority_guardian.ino
- Capture serial output during a run into
Task2_PriorityGuardian/rolling_average_logs.txt
- README should include build instructions, video link, and architecture
description.
*/
