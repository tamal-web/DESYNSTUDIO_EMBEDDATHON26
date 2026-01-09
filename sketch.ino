#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// RTOS Handles
QueueHandle_t commandQueue;
TaskHandle_t t_Input;
TaskHandle_t t_Display;
TaskHandle_t t_Blink;

// Shared Global Variable (protected by logic, or use a Mutex if you want extra points!)
int currentDelay = 1000; // Default 1 second blink

// Data Structure for the Queue
struct Command {
  char text[20];
  int blinkRate;
};

// ==========================================
// TASK 1: THE HEART (Blink LED)
// ==========================================
void HeartTask(void *pvParameters) {
  pinMode(2, OUTPUT);
  
  for (;;) {
    // Blink LED on for 100ms, then off for currentDelay interval
    digitalWrite(2, HIGH);
    vTaskDelay(100 / portTICK_PERIOD_MS);  // LED on for 100ms
    digitalWrite(2, LOW);
    vTaskDelay(currentDelay / portTICK_PERIOD_MS);  // Wait for currentDelay
  }
}

// ==========================================
// TASK 2: THE EAR (Serial Input)
// ==========================================
void InputTask(void *pvParameters) {
    Serial.begin(115200);
    
    for (;;) {
      if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        
        // JSON Parsing (Simplified)
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, input);
        
        if (!error) {
          Command cmd;
          
          // Extract data from JSON
          // 1. Copy doc["msg"] into cmd.text using strlcpy
          strlcpy(cmd.text, doc["msg"], sizeof(cmd.text));
          
          // 2. Copy doc["delay"] into cmd.blinkRate
          cmd.blinkRate = doc["delay"];
          
          // Send 'cmd' to 'commandQueue'
          xQueueSend(commandQueue, &cmd, portMAX_DELAY);
          
          Serial.println("Command sent to queue!");
        } else {
          Serial.println("JSON Error");
        }
      }
      vTaskDelay(50 / portTICK_PERIOD_MS); // Yield to other tasks
    }
}

// ==========================================
// TASK 3: THE FACE (OLED Display)
// ==========================================
void DisplayTask(void *pvParameters) {
    Command receivedCmd;
    
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
      for(;;);
    }
    
    // Initial Screen
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 20);
    display.println("Waiting...");
    display.display();
    
    for (;;) {
      // Receive from Queue and wait here indefinitely (portMAX_DELAY) until a message arrives
      if (xQueueReceive(commandQueue, &receivedCmd, portMAX_DELAY) == pdTRUE) {
        
        // Update the Global Variable for the Heart Task
        currentDelay = receivedCmd.blinkRate;
        
        // Updates the Screen
        display.clearDisplay();
        display.setCursor(0, 20);
        
        // Print the text received from the queue
        display.println(receivedCmd.text);
        display.display();
        
        Serial.println("Screen Updated.");
      }
    }
}

void setup() {
  // Create Queue of size 5, element size = sizeof(Command)
  commandQueue = xQueueCreate(5, sizeof(Command));
  
  // Create Tasks
  xTaskCreate(
    HeartTask,        // Task function
    "HeartTask",      // Task name
    2048,             // Stack size (bytes)
    NULL,             // Parameters
    1,                // Priority
    &t_Blink          // Task handle
  );
  
  xTaskCreate(
    InputTask,        // Task function
    "InputTask",      // Task name
    4096,             // Stack size (bytes)
    NULL,             // Parameters
    2,                // Priority (higher than Heart)
    &t_Input          // Task handle
  );
  
  xTaskCreate(
    DisplayTask,      // Task function
    "DisplayTask",    // Task name
    4096,             // Stack size (bytes)
    NULL,             // Parameters
    2,                // Priority (same as Input)
    &t_Display        // Task handle
  );
}

void loop() {
  // Empty - FreeRTOS tasks handle everything
}
