#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ===== CONFIGURE THESE =====
const char *ssid = "Airtel_Shri Radhey";              // Change this!
const char *password = "BankeyBihari@152";            // Change this!
const char *mqtt_server = "broker.mqttdashboard.com"; // From organizers
const int mqtt_port = 1883;
const char *mqtt_topic = "tamal/t1";

// LED Pins - CHANGE IF NEEDED
#define RED_LED 25
#define GREEN_LED 26
#define BLUE_LED 27

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// MQTT Client
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Task Handles
TaskHandle_t t_RedLED;
TaskHandle_t t_GreenLED;
TaskHandle_t t_BlueLED;
TaskHandle_t t_Display;
TaskHandle_t t_MQTT;

// Semaphores for thread-safe access
SemaphoreHandle_t redSemaphore;
SemaphoreHandle_t greenSemaphore;
SemaphoreHandle_t blueSemaphore;

// Timing Arrays
#define MAX_TIMING_ELEMENTS 50
int redTimings[MAX_TIMING_ELEMENTS];
int greenTimings[MAX_TIMING_ELEMENTS];
int blueTimings[MAX_TIMING_ELEMENTS];
int redCount = 0;
int greenCount = 0;
int blueCount = 0;

// Status
bool timingsReceived = false;
unsigned long startTime = 0;

// ==========================================
// WiFi Connection
// ==========================================
void setupWiFi() {
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✓ WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

// ==========================================
// MQTT Callback - Receives Messages
// ==========================================
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  Serial.println("\n=== NEW MESSAGE RECEIVED ===");
  Serial.print("Topic: ");
  Serial.println(topic);

  // Convert payload to string
  char json[2048];
  if (length >= sizeof(json))
    length = sizeof(json) - 1;
  memcpy(json, payload, length);
  json[length] = '\0';

  Serial.print("Message: ");
  Serial.println(json);

  // Parse JSON
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, json);

  if (error) {
    Serial.print("JSON Error: ");
    Serial.println(error.c_str());
    return;
  }

  // Extract timing arrays
  xSemaphoreTake(redSemaphore, portMAX_DELAY);
  JsonArray redArray = doc["red"];
  redCount = 0;
  for (int val : redArray) {
    if (redCount < MAX_TIMING_ELEMENTS) {
      redTimings[redCount++] = val;
    }
  }
  xSemaphoreGive(redSemaphore);
  if (t_RedLED != NULL)
    xTaskNotifyGive(t_RedLED);

  xSemaphoreTake(greenSemaphore, portMAX_DELAY);
  JsonArray greenArray = doc["green"];
  greenCount = 0;
  for (int val : greenArray) {
    if (greenCount < MAX_TIMING_ELEMENTS) {
      greenTimings[greenCount++] = val;
    }
  }
  xSemaphoreGive(greenSemaphore);
  if (t_GreenLED != NULL)
    xTaskNotifyGive(t_GreenLED);

  xSemaphoreTake(blueSemaphore, portMAX_DELAY);
  JsonArray blueArray = doc["blue"];
  blueCount = 0;
  for (int val : blueArray) {
    if (blueCount < MAX_TIMING_ELEMENTS) {
      blueTimings[blueCount++] = val;
    }
  }
  xSemaphoreGive(blueSemaphore);
  if (t_BlueLED != NULL)
    xTaskNotifyGive(t_BlueLED);

  timingsReceived = true;
  startTime = millis();

  Serial.println("✓ Timings Updated!");
  Serial.printf("Red: %d values | Green: %d values | Blue: %d values\n",
                redCount, greenCount, blueCount);
  Serial.println("============================\n");
}

// ==========================================
// MQTT Reconnect
// ==========================================
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT broker...");

    // Generate unique client ID
    String clientId = "ESP32-Shrimp-" + String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" ✓ Connected!");
      mqttClient.subscribe(mqtt_topic);
      Serial.print("Subscribed to: ");
      Serial.println(mqtt_topic);
    } else {
      Serial.print(" ✗ Failed! Error code: ");
      Serial.println(mqttClient.state());
      Serial.println("Retrying in 5 seconds...");
      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}

// ==========================================
// TASK: Red LED
// ==========================================
void RedLEDTask(void *pvParameters) {
  pinMode(RED_LED, OUTPUT);
  digitalWrite(RED_LED, HIGH); // Ensure OFF initially (Active Low)
  TickType_t xLastWakeTime;

  for (;;) {
    // Wait for new timing message
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Initialize xLastWakeTime at start of new pattern
    xLastWakeTime = xTaskGetTickCount();

    // Copy timing array safely
    xSemaphoreTake(redSemaphore, portMAX_DELAY);
    int count = redCount;
    int timings[MAX_TIMING_ELEMENTS];
    memcpy(timings, redTimings, sizeof(int) * count);
    xSemaphoreGive(redSemaphore);

    // Execute timing pattern
    for (int i = 0; i < count; i++) {
      digitalWrite(RED_LED, (i % 2 == 0) ? LOW : HIGH); // LOW is ON
      vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(timings[i]));
    }

    // Explicitly turn LED OFF and wait for next message
    digitalWrite(RED_LED, HIGH);
  }
}

// ==========================================
// TASK: Green LED
// ==========================================
void GreenLEDTask(void *pvParameters) {
  pinMode(GREEN_LED, OUTPUT);
  digitalWrite(GREEN_LED, HIGH); // Ensure OFF initially
  TickType_t xLastWakeTime;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    xLastWakeTime = xTaskGetTickCount();

    xSemaphoreTake(greenSemaphore, portMAX_DELAY);
    int count = greenCount;
    int timings[MAX_TIMING_ELEMENTS];
    memcpy(timings, greenTimings, sizeof(int) * count);
    xSemaphoreGive(greenSemaphore);

    for (int i = 0; i < count; i++) {
      digitalWrite(GREEN_LED, (i % 2 == 0) ? LOW : HIGH);
      vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(timings[i]));
    }

    digitalWrite(GREEN_LED, HIGH);
  }
}

// ==========================================
// TASK: Blue LED
// ==========================================
void BlueLEDTask(void *pvParameters) {
  pinMode(BLUE_LED, OUTPUT);
  digitalWrite(BLUE_LED, HIGH); // Ensure OFF initially
  TickType_t xLastWakeTime;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    xLastWakeTime = xTaskGetTickCount();

    xSemaphoreTake(blueSemaphore, portMAX_DELAY);
    int count = blueCount;
    int timings[MAX_TIMING_ELEMENTS];
    memcpy(timings, blueTimings, sizeof(int) * count);
    xSemaphoreGive(blueSemaphore);

    for (int i = 0; i < count; i++) {
      digitalWrite(BLUE_LED, (i % 2 == 0) ? LOW : HIGH);
      vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(timings[i]));
    }

    digitalWrite(BLUE_LED, HIGH);
  }
}

// ==========================================
// TASK: Display Status
// ==========================================
void DisplayTask(void *pvParameters) {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Display init FAILED!");
    for (;;)
      vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Timing Keeper");
  display.println("Initializing...");
  display.display();

  for (;;) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);

    display.println("=TIMING KEEPER=");
    display.println("---------------");

    if (timingsReceived) {
      unsigned long elapsed = (millis() - startTime) / 1000;
      display.printf("Time: %lu sec\n", elapsed);
      display.printf("R:%d G:%d B:%d\n", redCount, greenCount, blueCount);
      display.println("Status: ACTIVE");
      display.println("LEDs Blinking!");
    } else {
      display.println("Waiting for");
      display.println("MQTT message...");
      display.println("");
      display.println("Ready to receive");
    }

    display.display();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// TASK: MQTT Handler
// ==========================================
void MQTTTask(void *pvParameters) {
  for (;;) {
    if (!mqttClient.connected()) {
      reconnectMQTT();
    }
    mqttClient.loop();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=================================");
  Serial.println("   SHRIMP TIMING KEEPER v1.0");
  Serial.println("=================================\n");

  // Setup WiFi
  setupWiFi();

  // Setup MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(2048);

  Serial.println("\nMQTT Configuration:");
  Serial.printf("Broker: %s:%d\n", mqtt_server, mqtt_port);
  Serial.printf("Topic: %s\n\n", mqtt_topic);

  // Create Semaphores
  redSemaphore = xSemaphoreCreateMutex();
  greenSemaphore = xSemaphoreCreateMutex();
  blueSemaphore = xSemaphoreCreateMutex();

  // Create FreeRTOS Tasks
  xTaskCreate(RedLEDTask, "RedLED", 4096, NULL, 3, &t_RedLED);
  xTaskCreate(GreenLEDTask, "GreenLED", 4096, NULL, 3, &t_GreenLED);
  xTaskCreate(BlueLEDTask, "BlueLED", 4096, NULL, 3, &t_BlueLED);
  xTaskCreate(DisplayTask, "Display", 4096, NULL, 2, &t_Display);
  xTaskCreate(MQTTTask, "MQTT", 4096, NULL, 4, &t_MQTT);

  Serial.println("✓ All FreeRTOS tasks created!");
  Serial.println("✓ System ready - waiting for timing commands...\n");
}

void loop() {
  // Empty - FreeRTOS handles everything
}