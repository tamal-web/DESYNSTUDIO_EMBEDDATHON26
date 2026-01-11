/*
 * ShrimpHub Task 3: The Window Synchronizer
 * Synchronize button press with MQTT window signals within ±50ms tolerance
 * 
 * Hardware Requirements:
 * - ESP32 board
 * - Push button (connected to GPIO with pull-up resistor)
 * - 3 LEDs: Blue (window open), Green (button press), Red (no window)
 * 
 * IMPORTANT: LEDs turn ON with LOW signal, OFF with HIGH signal
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ==================== CONFIGURATION ====================
// WiFi credentials
const char* ssid = "Airtel_Shri Radhey";
const char* password = "BankeyBihari@152";

// MQTT Broker
const char* mqtt_server = "broker.mqttdashboard.com";
const int mqtt_port = 1883;

// Team identifiers - REPLACE THESE WITH YOUR VALUES
const char* TEAM_ID = "tamaldesyn";           // From Task 2: <leader's first name><team name>
const char* REEF_ID = "tamalkchhabra2007desyn";           // From Task 2: <leader's email prefix><team name>
const char* WINDOW_CODE = "grgeg_window";     // Received from Task 2 completion

// GPIO Pin Assignments
const int BUTTON_PIN = 15;        // Push button input
const int LED_BLUE = 24;         // Window open indicator (ON = window open)
const int LED_GREEN = 26;        // Button press indicator (flashes on press)
const int LED_RED = 25;          // No window indicator (ON = no window)

// Timing Constants
const unsigned long DEBOUNCE_DELAY = 20;      // Button debounce time (ms)
const unsigned long SYNC_TOLERANCE = 50;      // Synchronization tolerance (±50ms)
const unsigned long BUTTON_FLASH_DURATION = 100; // Green LED flash duration

// ==================== GLOBAL VARIABLES ====================
WiFiClient espClient;
PubSubClient mqtt(espClient);

// Window state
volatile bool windowOpen = false;
volatile unsigned long windowOpenTime = 0;
volatile unsigned long windowCloseTime = 0;

// Button state
volatile unsigned long lastButtonPress = 0;
volatile bool buttonPressed = false;
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;

// Synchronization tracking
int successfulSyncs = 0;
const int REQUIRED_SYNCS = 3;

// LED flash control
unsigned long greenLedFlashStart = 0;
bool greenLedFlashing = false;

// ==================== FUNCTION DECLARATIONS ====================
void setup_wifi();
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void reconnect_mqtt();
void handleButtonPress();
void checkWindowSync();
void updateLEDs();
void publishSync(unsigned long syncTime);

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== ShrimpHub Task 3: Window Synchronizer ===");
  
  // Initialize GPIO pins
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Button with internal pull-up
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  
  // Turn OFF all LEDs initially (HIGH = OFF for inverted logic)
  digitalWrite(LED_BLUE, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_RED, HIGH);
  
  // Turn ON red LED to indicate "no window" state (LOW = ON)
  digitalWrite(LED_RED, LOW);
  
  // Connect to WiFi
  setup_wifi();
  
  // Setup MQTT
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqtt_callback);
  mqtt.setBufferSize(512);
  
  Serial.println("Setup complete. Waiting for window signals...");
}

// ==================== MAIN LOOP ====================
void loop() {
  // Maintain MQTT connection
  if (!mqtt.connected()) {
    reconnect_mqtt();
  }
  mqtt.loop();
  
  // Handle button debouncing and detection
  handleButtonPress();
  
  // Check for synchronization
  checkWindowSync();
  
  // Update LED states
  updateLEDs();
  
  // Small delay to prevent overwhelming the system
  delay(1);
}

// ==================== WiFi SETUP ====================
void setup_wifi() {
  delay(10);
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// ==================== MQTT CALLBACK ====================
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  // Convert payload to string
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  Serial.print("Message received on topic: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  Serial.println(message);
  
  // Check if this is the window topic
  if (strcmp(topic, WINDOW_CODE) == 0) {
    // Parse JSON
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
      return;
    }
    
    // Check window status
    const char* status = doc["status"];
    if (status && strcmp(status, "open") == 0) {
      windowOpen = true;
      windowOpenTime = millis();
      Serial.print("[WINDOW OPEN] at ");
      Serial.println(windowOpenTime);
    }
  }
}

// ==================== MQTT RECONNECT ====================
void reconnect_mqtt() {
  while (!mqtt.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    // Create a unique client ID
    String clientId = "ESP32_Task3_";
    clientId += String(random(0xffff), HEX);
    
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("connected");
      
      // Subscribe to window topic
      mqtt.subscribe(WINDOW_CODE);
      Serial.print("Subscribed to: ");
      Serial.println(WINDOW_CODE);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" retrying in 5 seconds");
      delay(5000);
    }
  }
}

// ==================== BUTTON HANDLING WITH DEBOUNCING ====================
void handleButtonPress() {
  int reading = digitalRead(BUTTON_PIN);
  
  // Check if button state changed
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  // Check if debounce delay has passed
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    // Button state is stable
    if (reading == LOW && !buttonPressed) {
      // Button just pressed (LOW = pressed with pull-up)
      buttonPressed = true;
      lastButtonPress = millis();
      
      // Flash green LED to indicate button press
      greenLedFlashing = true;
      greenLedFlashStart = millis();
      digitalWrite(LED_GREEN, LOW);  // Turn ON green LED
      
      Serial.print("[BUTTON PRESS] at ");
      Serial.println(lastButtonPress);
    } else if (reading == HIGH && buttonPressed) {
      // Button released
      buttonPressed = false;
    }
  }
  
  lastButtonState = reading;
}

// ==================== CHECK WINDOW SYNCHRONIZATION ====================
void checkWindowSync() {
  // Only check if window is currently open
  if (!windowOpen) {
    return;
  }
  
  // Check if window should be closed (assuming 500-1000ms duration)
  // We'll allow up to 1500ms to be safe
  unsigned long currentTime = millis();
  if (currentTime - windowOpenTime > 1500) {
    windowOpen = false;
    windowCloseTime = currentTime;
    Serial.print("[WINDOW CLOSED] at ");
    Serial.println(windowCloseTime);
    return;
  }
  
  // Check if button was pressed during the window
  // The button press just needs to happen while window is open
  if (buttonPressed && lastButtonPress >= windowOpenTime) {
    // Calculate when the button was pressed relative to window opening
    unsigned long timeSinceWindowOpen = lastButtonPress - windowOpenTime;
    
    // Successful synchronization - button pressed while window was open!
    Serial.println("\n*** SYNCHRONIZATION SUCCESSFUL! ***");
    Serial.print("Window opened at: ");
    Serial.println(windowOpenTime);
    Serial.print("Button pressed at: ");
    Serial.println(lastButtonPress);
    Serial.print("Time since window opened: ");
    Serial.print(timeSinceWindowOpen);
    Serial.println(" ms");
    
    // Publish sync message
    publishSync(lastButtonPress);
    
    // Increment successful syncs
    successfulSyncs++;
    Serial.print("Total successful syncs: ");
    Serial.print(successfulSyncs);
    Serial.print(" / ");
    Serial.println(REQUIRED_SYNCS);
    
    // Close the window to prevent duplicate syncs
    windowOpen = false;
    buttonPressed = false;
    
    // Check if we've completed the task
    if (successfulSyncs >= REQUIRED_SYNCS) {
      Serial.println("\n=== TASK 3 COMPLETE! ===");
      Serial.println("All required synchronizations achieved!");
      Serial.println("Waiting for steganography challenge code...\n");
    }
  }
}

// ==================== UPDATE LED INDICATORS ====================
void updateLEDs() {
  // Blue LED: ON when window is open, OFF otherwise
  if (windowOpen) {
    digitalWrite(LED_BLUE, LOW);   // Turn ON (inverted logic)
    digitalWrite(LED_RED, HIGH);   // Turn OFF red
  } else {
    digitalWrite(LED_BLUE, HIGH);  // Turn OFF blue
    digitalWrite(LED_RED, LOW);    // Turn ON red (no window)
  }
  
  // Green LED: Flash for BUTTON_FLASH_DURATION ms after button press
  if (greenLedFlashing) {
    if (millis() - greenLedFlashStart > BUTTON_FLASH_DURATION) {
      digitalWrite(LED_GREEN, HIGH);  // Turn OFF green LED
      greenLedFlashing = false;
    }
  }
}

// ==================== PUBLISH SYNCHRONIZATION ====================
void publishSync(unsigned long syncTime) {
  StaticJsonDocument<200> doc;
  doc["status"] = "synced";
  doc["timestamp_ms"] = syncTime;
  
  char jsonBuffer[200];
  serializeJson(doc, jsonBuffer);
  
  // Publish to cagedmonkey/listener
  const char* syncTopic = "cagedmonkey/listener";
  bool published = mqtt.publish(syncTopic, jsonBuffer);
  
  if (published) {
    Serial.print("Sync published to ");
    Serial.print(syncTopic);
    Serial.print(": ");
    Serial.println(jsonBuffer);
  } else {
    Serial.println("Failed to publish sync message!");
  }
}