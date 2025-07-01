/**
 * @file      esp_now_receiver_enhanced.ino
 * @author    Enhanced Version
 * @date      2025-06-29
 * @brief     Enhanced ESP-NOW receiver for ESP32 WROOM with beautiful LED effects
 * 
 * MAJOR FIXES:
 * - Fixed request/response system with proper peer management
 * - Improved serial command handling with better parsing
 * - Enhanced LED effects with smoother animations
 * - Added comprehensive status reporting and diagnostics
 * - Beautiful startup sequence and error handling
 * - Optimized performance with better timing control
 */

#include <WiFi.h>
#include <esp_now.h>
#include <FastLED.h>
#include <esp_log.h>
#include <esp_wifi.h>

String repeat(String str, int count) {
  String result = "";
  for (int i = 0; i < count; i++) {
    result += str;
  }
  return result;
}

// =============================================================================
// CONFIGURATION & HARDWARE SETUP
// =============================================================================
#define LED_PIN         13
#define LED_WIDTH       32
#define LED_HEIGHT      8
#define NUM_LEDS        (LED_WIDTH * LED_HEIGHT)
#define LED_TYPE        WS2812B
#define COLOR_ORDER     GRB

// Performance & Timing
#define LED_UPDATE_INTERVAL_MS    33  // ~30 FPS for smooth animations
#define SERIAL_BAUD_RATE         115200
#define REQUEST_TIMEOUT_MS       3000
#define HEARTBEAT_INTERVAL_MS    10000

// =============================================================================
// DATA STRUCTURES (Must match transmitter exactly)
// =============================================================================
typedef struct {
    uint8_t requestType;  // 1 = color request
    uint8_t fromReceiver; // 1 = marks request origin
} color_request_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t white;
    uint8_t warmWhite;
    uint8_t brightness;
    uint8_t effect;
    uint8_t speed;
} led_command_t;

typedef struct {
    uint8_t requestType;  // 2 = serial data
    char data[50];       // Adjust size as needed
    uint8_t length;       // Actual length of the data
} serial_message_t;

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================
CRGB leds[NUM_LEDS];

// Communication
uint8_t controllerAddress[] = {0x64, 0xE8, 0x33, 0x7A, 0x88, 0x70}; // UPDATE THIS!
volatile led_command_t receivedCommand;
volatile bool newCommandReceived = false;
bool expectingResponse = false;
unsigned long responseTimeout = 0;
unsigned long lastHeartbeat = 0;

// Performance tracking
unsigned long lastLedUpdateTime = 0;
unsigned long commandsReceived = 0;
unsigned long requestsSent = 0;
bool isConnected = false;

// LED State Management
uint8_t currentEffect = 0;
uint8_t currentSpeed = 50;
uint8_t currentBrightness = 50;
CRGB currentColor = CRGB::Red;
unsigned long lastEffectRunTime = 0;

// Effect-specific variables
uint8_t rainbowHue = 0;
bool strobeState = false;
unsigned long fadeStartTime = 0;
CRGB fadeStartColor = CRGB::Black;
CRGB fadeTargetColor = CRGB::Red;
bool fadingIn = true;
float pulsePhase = 0.0;

// =============================================================================
// FUNCTION PROTOTYPES
// =============================================================================
void initializeHardware();
void initializeESPNOW();
void setupPeerConnection();
void handleSerialCommands();
void processReceivedCommand();
void updateLEDEffects();
void sendColorRequest();
void printStatus();
void printDiagnostics();

// LED Effects
void applyEffect();
void effectSolid();
void effectRainbow();
void effectFade();
void effectStrobe();
void effectPulse();
void effectSparkle();
void effectWave();
CRGB applyWhiteAndWarmWhite(CRGB color, uint8_t white, uint8_t warmWhite);

// Utility functions
void bootSequence();
void showError(const char* message);
void showSuccess(const char* message);
int16_t getMatrixIndex(int16_t x, int16_t y);

// =============================================================================
// ESP-NOW CALLBACKS
// =============================================================================
void OnDataRecv(const esp_now_recv_info *recv_info, const uint8_t *incomingData, int len) {
    // Verify sender MAC address for security
    bool validSender = true;
    for (int i = 0; i < 6; i++) {
        if (recv_info->src_addr[i] != controllerAddress[i]) {
            validSender = false;
            break;
        }
    }
    
    if (!validSender) {
        Serial.println("⚠️  Ignoring data from unknown sender");
        return;
    }

    if (len == sizeof(led_command_t)) {
        memcpy((void*)&receivedCommand, incomingData, sizeof(receivedCommand));
        newCommandReceived = true;
        expectingResponse = false;
        isConnected = true;
        commandsReceived++;
        
        Serial.printf("📨 Command received: R:%d G:%d B:%d Effect:%d\n", 
                     receivedCommand.red, receivedCommand.green, 
                     receivedCommand.blue, receivedCommand.effect);
    } else {
        Serial.printf("⚠️  Invalid data length: %d (expected %d)\n", len, sizeof(led_command_t));
    }

    if (len >= sizeof(serial_message_t)) {
        serial_message_t serialMsg;
        memcpy(&serialMsg, incomingData, sizeof(serialMsg));
        
        if (serialMsg.requestType == 2) {  // It's serial data
            Serial.printf("Received serial data (%.*s)\n", serialMsg.length, serialMsg.data);
            
            // Process the received serial data here
            // For example, you could echo it to the local serial port:
            Serial.write((uint8_t*)serialMsg.data, serialMsg.length);
            Serial.println();
        }
    }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("✅ Request sent successfully");
        expectingResponse = true;
        responseTimeout = millis() + REQUEST_TIMEOUT_MS;
    } else {
        Serial.printf("❌ Request send failed: %d\n", status);
        expectingResponse = false;
    }
}

// =============================================================================
// INITIALIZATION FUNCTIONS
// =============================================================================
void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(1000);
    
    Serial.println("\n" + repeat("=", 60));
    Serial.println("🚀 ESP-NOW LED RECEIVER - Enhanced Version");
    Serial.println(repeat("=", 60));
    
    initializeHardware();
    initializeESPNOW();
    bootSequence();
    
    Serial.println("✅ System ready! Type 'help' for commands\n");
}

void initializeHardware() {
    Serial.println("🔧 Initializing hardware...");
    
    // Configure ESP-NOW log levels
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("esp_now", ESP_LOG_WARN);
    
    // Initialize FastLED
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(50);
    FastLED.clear();
    FastLED.show();
    
    Serial.println("  ✓ FastLED initialized");
    Serial.println("  ✓ LED matrix configured (32x8)");
}

void initializeESPNOW() {
    Serial.println("📡 Initializing ESP-NOW...");
    
    WiFi.mode(WIFI_STA);
    delay(100);
    
    Serial.printf("  📍 MAC Address: %s\n", WiFi.macAddress().c_str());
    
    if (esp_now_init() != ESP_OK) {
        showError("ESP-NOW initialization failed!");
        return;
    }
    
    // Register callbacks
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);
    
    setupPeerConnection();
    Serial.println("  ✅ ESP-NOW ready");
}

void setupPeerConnection() {
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, controllerAddress, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("  ❌ Failed to add controller peer");
    } else {
        Serial.printf("  ✅ Controller peer added: ");
        for(int i = 0; i < 6; i++) {
            Serial.printf("%02X", controllerAddress[i]);
            if(i < 5) Serial.print(":");
        }
        Serial.println();
    }
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
    handleSerialCommands();
    processReceivedCommand();
    updateLEDEffects();
    
    // Handle response timeout
    if (expectingResponse && millis() > responseTimeout) {
        expectingResponse = false;
        isConnected = false;
        Serial.println("⏰ Response timeout - controller may be offline");
    }
    
    // Send periodic heartbeat
    if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL_MS) {
        if (!expectingResponse) {
            Serial.println("💓 Sending heartbeat request...");
            sendColorRequest();
        }
        lastHeartbeat = millis();
    }
    
    delay(5);
}

// =============================================================================
// COMMAND PROCESSING
// =============================================================================
void handleSerialCommands() {
    if (!Serial.available()) return;
    
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();
    
    if (command.length() == 0) return;
    
    Serial.println("📝 Command: " + command);
    
    if (command == "request" || command == "r") {
        sendColorRequest();
    } 
    else if (command == "status" || command == "s") {
        printStatus();
    }
    else if (command == "diag" || command == "d") {
        printDiagnostics();
    }
    else if (command == "test" || command == "t") {
        bootSequence();
    }
    else if (command == "clear" || command == "c") {
        FastLED.clear();
        FastLED.show();
        Serial.println("🔄 LEDs cleared");
    }
    else if (command == "help" || command == "h") {
        printHelp();
    }
    else if (command.startsWith("bright ")) {
        int brightness = command.substring(7).toInt();
        if (brightness >= 1 && brightness <= 100) {
            currentBrightness = brightness;
            FastLED.setBrightness(map(brightness, 1, 100, 0, 255));
            Serial.printf("☀️  Brightness set to %d%%\n", brightness);
        } else {
            Serial.println("❌ Brightness must be 1-100");
        }
    }
    else if (command.startsWith("effect ")) {
        int effect = command.substring(7).toInt();
        if (effect >= 0 && effect <= 6) {
            currentEffect = effect;
            Serial.printf("✨ Effect set to %d\n", effect);
        } else {
            Serial.println("❌ Effect must be 0-6");
        }
    }
    else {
        Serial.println("❓ Unknown command. Type 'help' for available commands.");
    }
}

void processReceivedCommand() {
    if (!newCommandReceived) return;
    
    newCommandReceived = false;
    
    // Update current state
    currentColor = CRGB(receivedCommand.red, receivedCommand.green, receivedCommand.blue);
    currentEffect = receivedCommand.effect;
    currentSpeed = receivedCommand.speed;
    currentBrightness = receivedCommand.brightness;
    
    // Reset effect states for smooth transitions
    rainbowHue = 0;
    strobeState = false;
    fadeStartTime = millis();
    fadingIn = true;
    fadeStartColor = CRGB::Black;
    fadeTargetColor = currentColor;
    pulsePhase = 0.0;
    
    Serial.printf("🎨 Updated: Color(%d,%d,%d) Effect:%d Speed:%d Brightness:%d%%\n",
                 currentColor.r, currentColor.g, currentColor.b,
                 currentEffect, currentSpeed, currentBrightness);
}

void updateLEDEffects() {
    if (millis() - lastLedUpdateTime < LED_UPDATE_INTERVAL_MS) return;
    
    lastLedUpdateTime = millis();
    FastLED.clear();
    FastLED.setBrightness(map(currentBrightness, 1, 100, 0, 255));
    
    applyEffect();
    FastLED.show();
}

// =============================================================================
// LED EFFECTS
// =============================================================================
void applyEffect() {
    switch (currentEffect) {
        case 0: effectSolid(); break;
        case 1: effectRainbow(); break;
        case 2: effectFade(); break;
        case 3: effectStrobe(); break;
        case 4: effectPulse(); break;
        case 5: effectSparkle(); break;
        case 6: effectWave(); break;
        default: effectSolid(); break;
    }
}

void effectSolid() {
    CRGB adjustedColor = applyWhiteAndWarmWhite(currentColor, receivedCommand.white, receivedCommand.warmWhite);
    fill_solid(leds, NUM_LEDS, adjustedColor);
}

void effectRainbow() {
    uint16_t speedFactor = map(currentSpeed, 1, 100, 200, 20);
    uint8_t hueOffset = (millis() / speedFactor) % 256;
    
    for (int i = 0; i < NUM_LEDS; i++) {
        CRGB rainbowColor = CHSV(hueOffset + (i * 256 / NUM_LEDS), 255, 255);
        leds[i] = applyWhiteAndWarmWhite(rainbowColor, receivedCommand.white, receivedCommand.warmWhite);
    }
}

void effectFade() {
    unsigned long fadeDuration = map(currentSpeed, 1, 100, 3000, 300);
    unsigned long elapsed = millis() - fadeStartTime;
    
    if (elapsed >= fadeDuration) {
        fadingIn = !fadingIn;
        fadeStartTime = millis();
        CRGB adjustedColor = applyWhiteAndWarmWhite(currentColor, receivedCommand.white, receivedCommand.warmWhite);
        fadeStartColor = fadingIn ? CRGB::Black : adjustedColor;
        fadeTargetColor = fadingIn ? adjustedColor : CRGB::Black;
        elapsed = 0;
    }
    
    float progress = constrain((float)elapsed / fadeDuration, 0.0, 1.0);
    // Use sine wave for smoother easing
    progress = (sin(progress * PI - PI/2) + 1.0) / 2.0;
    
    CRGB interpolatedColor = blend(fadeStartColor, fadeTargetColor, (uint8_t)(progress * 255));
    fill_solid(leds, NUM_LEDS, interpolatedColor);
}

void effectStrobe() {
    unsigned long strobeDelay = map(currentSpeed, 1, 100, 800, 30);
    if (millis() - lastEffectRunTime >= strobeDelay) {
        lastEffectRunTime = millis();
        strobeState = !strobeState;
    }
    
    CRGB strobeColor = strobeState ? 
                      applyWhiteAndWarmWhite(currentColor, receivedCommand.white, receivedCommand.warmWhite) : 
                      CRGB::Black;
    fill_solid(leds, NUM_LEDS, strobeColor);
}

void effectPulse() {
    unsigned long pulsePeriod = map(currentSpeed, 1, 100, 4000, 400);
    pulsePhase = (float)(millis() % pulsePeriod) / pulsePeriod * TWO_PI;
    float brightnessFactor = (sin(pulsePhase) + 1.0) / 2.0;
    
    // Apply smooth cubic easing
    brightnessFactor = brightnessFactor * brightnessFactor * (3.0 - 2.0 * brightnessFactor);
    
    CRGB baseColor = applyWhiteAndWarmWhite(currentColor, receivedCommand.white, receivedCommand.warmWhite);
    CRGB pulsedColor = baseColor;
    pulsedColor.nscale8_video(brightnessFactor * 255);
    fill_solid(leds, NUM_LEDS, pulsedColor);
}

void effectSparkle() {
    // Fade existing sparkles
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i].nscale8(240); // Fade by ~6%
    }
    
    // Add new sparkles based on speed
    int sparkleCount = map(currentSpeed, 1, 100, 1, 8);
    CRGB sparkleColor = applyWhiteAndWarmWhite(currentColor, receivedCommand.white, receivedCommand.warmWhite);
    
    for (int i = 0; i < sparkleCount; i++) {
        if (random(100) < 30) { // 30% chance per sparkle
            int pos = random(NUM_LEDS);
            leds[pos] = sparkleColor;
        }
    }
}

void effectWave() {
    unsigned long waveSpeed = map(currentSpeed, 1, 100, 100, 10);
    float timeOffset = (float)millis() / waveSpeed;
    
    CRGB waveColor = applyWhiteAndWarmWhite(currentColor, receivedCommand.white, receivedCommand.warmWhite);
    
    for (int x = 0; x < LED_WIDTH; x++) {
        for (int y = 0; y < LED_HEIGHT; y++) {
            float wave1 = sin((x * 0.3) + timeOffset);
            float wave2 = sin((y * 0.5) + timeOffset * 1.2);
            float brightness = (wave1 + wave2 + 2.0) / 4.0; // Normalize to 0-1
            
            CRGB pixelColor = waveColor;
            pixelColor.nscale8_video(brightness * 255);
            
            int index = getMatrixIndex(x, y);
            if (index >= 0 && index < NUM_LEDS) {
                leds[index] = pixelColor;
            }
        }
    }
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================
CRGB applyWhiteAndWarmWhite(CRGB color, uint8_t white, uint8_t warmWhite) {
    CHSV hsv = rgb2hsv_approximate(color);
    hsv.s = map(white, 0, 255, 255, 0); // Higher white = less saturation
    
    CRGB saturatedColor;
    hsv2rgb_spectrum(hsv, saturatedColor);
    
    if (warmWhite > 0) {
        CRGB warmColor = CRGB(255, map(warmWhite, 0, 255, 120, 255), 20);
        saturatedColor = blend(saturatedColor, warmColor, warmWhite >> 2); // Subtle warm blend
    }
    
    return saturatedColor;
}

int16_t getMatrixIndex(int16_t x, int16_t y) {
    if (x < 0 || x >= LED_WIDTH || y < 0 || y >= LED_HEIGHT) return -1;
    
    // Serpentine layout: even rows left-to-right, odd rows right-to-left
    if (y % 2 == 0) {
        return y * LED_WIDTH + x;
    } else {
        return y * LED_WIDTH + (LED_WIDTH - 1 - x);
    }
}

void sendColorRequest() {
    if (expectingResponse) {
        Serial.println("⏳ Already waiting for response...");
        return;
    }
    
    color_request_t request = {1, 1}; // requestType=1, fromReceiver=1
    
    Serial.println("📤 Sending color request...");
    esp_err_t result = esp_now_send(controllerAddress, (uint8_t*)&request, sizeof(request));
    
    if (result == ESP_OK) {
        requestsSent++;
        Serial.println("✅ Request queued successfully");
    } else {
        Serial.printf("❌ Request failed: 0x%X\n", result);
    }
}

// =============================================================================
// DISPLAY & DIAGNOSTIC FUNCTIONS
// =============================================================================
void bootSequence() {
    Serial.println("🌈 Running boot sequence...");
    
    const CRGB colors[] = {CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::Yellow, CRGB::Cyan, CRGB::Magenta};
    const int numColors = sizeof(colors) / sizeof(colors[0]);
    
    FastLED.setBrightness(100);
    
    // Color sweep
    for (int c = 0; c < numColors; c++) {
        fill_solid(leds, NUM_LEDS, colors[c]);
        FastLED.show();
        delay(300);
    }
    
    // Wave effect
    for (int wave = 0; wave < 20; wave++) {
        for (int i = 0; i < NUM_LEDS; i++) {
            float brightness = (sin(i * 0.3 + wave * 0.5) + 1.0) / 2.0;
            leds[i] = CRGB(brightness * 255, brightness * 100, brightness * 255);
        }
        FastLED.show();
        delay(50);
    }
    
    // Fade to black
    for (int brightness = 255; brightness >= 0; brightness -= 5) {
        FastLED.setBrightness(brightness);
        FastLED.show();
        delay(20);
    }
    
    FastLED.clear();
    FastLED.setBrightness(map(currentBrightness, 1, 100, 0, 255));
    FastLED.show();
    
    Serial.println("✨ Boot sequence complete!");
}

void printStatus() {
    Serial.println("\n" + repeat("━", 50));
    Serial.println("📊 RECEIVER STATUS");
    Serial.println(repeat("━", 50));
    Serial.printf("🔗 Connection: %s\n", isConnected ? "✅ Connected" : "❌ Disconnected");
    Serial.printf("📨 Commands received: %lu\n", commandsReceived);
    Serial.printf("📤 Requests sent: %lu\n", requestsSent);
    Serial.printf("⏳ Expecting response: %s\n", expectingResponse ? "Yes" : "No");
    Serial.printf("💾 Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.println(repeat("━", 50));
    Serial.printf("🎨 Current color: RGB(%d, %d, %d)\n", currentColor.r, currentColor.g, currentColor.b);
    Serial.printf("✨ Effect: %d | Speed: %d | Brightness: %d%%\n", currentEffect, currentSpeed, currentBrightness);
    Serial.println(repeat("━", 50) + "\n");
}

void printDiagnostics() {
    Serial.println("\n" + repeat("🔧", 20) + " DIAGNOSTICS " + repeat("🔧", 20));
    
    // WiFi info
    Serial.printf("📡 WiFi Mode: %d\n", WiFi.getMode());
    Serial.printf("📍 MAC Address: %s\n", WiFi.macAddress().c_str());
    Serial.printf("📶 WiFi Status: %d\n", WiFi.status());
    
    // ESP-NOW info
    Serial.println("\n🔄 ESP-NOW Peer Info:");
    Serial.printf("  Controller MAC: ");
    for(int i = 0; i < 6; i++) {
        Serial.printf("%02X", controllerAddress[i]);
        if(i < 5) Serial.print(":");
    }
    Serial.println();
    
    // Hardware info
    Serial.printf("\n💾 Memory Info:\n");
    Serial.printf("  Free Heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("  Min Free Heap: %d bytes\n", ESP.getMinFreeHeap());
    Serial.printf("  Max Alloc Heap: %d bytes\n", ESP.getMaxAllocHeap());
    
    Serial.printf("\n⚡ System Info:\n");
    Serial.printf("  CPU Frequency: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("  Flash Size: %d bytes\n", ESP.getFlashChipSize());
    Serial.printf("  Uptime: %lu seconds\n", millis() / 1000);
    
    Serial.println("\n" + repeat("🔧", 55) + "\n");
}

void printHelp() {
    Serial.println("\n" + repeat("📚", 25) + " HELP " + repeat("📚", 25));
    Serial.println("Available Commands:");
    Serial.println("  request, r     - Request color data from controller");
    Serial.println("  status, s      - Show connection and LED status");
    Serial.println("  diag, d        - Show detailed diagnostics");
    Serial.println("  test, t        - Run LED test sequence");
    Serial.println("  clear, c       - Turn off all LEDs");
    Serial.println("  help, h        - Show this help message");
    Serial.println("  bright <1-100> - Set brightness (e.g., 'bright 75')");
    Serial.println("  effect <0-6>   - Set effect (0=Solid, 1=Rainbow, 2=Fade, 3=Strobe, 4=Pulse, 5=Sparkle, 6=Wave)");
    Serial.println("\nEffects:");
    Serial.println("  0 - Solid Color    4 - Pulse");
    Serial.println("  1 - Rainbow        5 - Sparkle");
    Serial.println("  2 - Fade           6 - Wave");
    Serial.println("  3 - Strobe");
    Serial.println("\n" + repeat("📚", 58) + "\n");
}

void showError(const char* message) {
    Serial.printf("❌ ERROR: %s\n", message);
    // Flash red LEDs to indicate error
    for (int i = 0; i < 3; i++) {
        fill_solid(leds, NUM_LEDS, CRGB::Red);
        FastLED.show();
        delay(200);
        FastLED.clear();
        FastLED.show();
        delay(200);
    }
}

void showSuccess(const char* message) {
    Serial.printf("✅ SUCCESS: %s\n", message);
    // Brief green flash
    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show();
    delay(300);
    FastLED.clear();
    FastLED.show();
}
