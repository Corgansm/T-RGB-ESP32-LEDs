/**
 * @file      esp_now_receiver.ino
 * @author    Lewis He (lewishe@outlook.com) - Modified by Gemini
 * @license   MIT
 * @copyright Copyright (c) 2024  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2024-06-26
 * @brief     ESP-NOW receiver for ESP32 WROOM to control an 8x32 LED matrix based on received LED command structure.
 * Restored compatibility with led_command_t, includes initial LED test pattern, moves FastLED.show() to main loop,
 * and suppresses verbose WiFi/ESP-NOW logs. Now includes implementation for various LED effects and speed control.
 */

#include <WiFi.h>
#include <esp_now.h>
#include <FastLED.h> // Include FastLED library for controlling the LED matrix
#include <esp_log.h> // Include for esp_log_level_set

// Define the LED matrix properties
#define LED_PIN     13   // Data pin for the LED matrix (Recommended pin for WS2812B on ESP32)
#define LED_WIDTH   32   // Width of the LED matrix
#define LED_HEIGHT  8    // Height of the LED matrix
#define NUM_LEDS    (LED_WIDTH * LED_HEIGHT) // Total number of LEDs
#define LED_TYPE    WS2812B // Type of your LED strip
#define COLOR_ORDER GRB  // Color order of your LED strip (often GRB or RGB)

// Rate limiting for LED updates
const unsigned long LED_UPDATE_INTERVAL_MS = 50; // Update LEDs at most every 50ms (20 times per second)
unsigned long lastLedUpdateTime = 0; // Tracks the last time LEDs were updated

CRGB leds[NUM_LEDS]; // Define the LED array, where LED data will be stored

// LED Control Structure - MUST MATCH THE TRANSMITTER'S STRUCTURE
// This structure defines the data format for commands sent via ESP-NOW.
typedef struct {
    uint8_t red;        // Red component of the color (0-255)
    uint8_t green;      // Green component of the color (0-255)
    uint8_t blue;       // Blue component of the color (0-255)
    uint8_t white;      // Not used for pure RGB matrix (WS2812B), but kept for compatibility
    uint8_t warmWhite;  // Not used for pure RGB matrix (WS2812B), but kept for compatibility
    uint8_t brightness; // Overall brightness (1-100 from transmitter, mapped to 0-255 for FastLED)
    uint8_t effect;     // LED effect to apply (0: Solid, 1: Rainbow, 2: Fade, 3: Strobe, 4: Pulse)
    uint8_t speed;      // Speed of the effect (1-100, maps to effect-specific timing)
} led_command_t;

// Use volatile for variables modified in an Interrupt Service Routine (ISR)
// and accessed in the main loop to ensure correct memory access.
volatile led_command_t receivedCommand; // Stores the most recently received LED command
volatile bool newCommandReceived = false; // Flag to signal that new data has been received

// Global variables for effect management and state persistence
uint8_t currentEffect = 0; // Stores the currently active effect (default: Solid)
uint8_t currentSpeed = 50; // Stores the current speed setting (default: 50)
CRGB currentColor; // Stores the base color received from the transmitter
unsigned long lastEffectRunTime = 0; // Timestamp for the last update of an effect (used for timing)
uint8_t rainbowHue = 0; // Current hue for the rainbow effect
bool strobeState = false; // Current state (on/off) for the strobe effect
unsigned long fadeStartTime = 0; // Timestamp when the current fade phase started
CRGB fadeStartColor; // Starting color for the current fade phase
CRGB fadeTargetColor; // Target color for the current fade phase
bool fadingIn = true; // Direction of fade (true: fading to color, false: fading to black)

// Function prototypes to declare functions before they are defined
void OnDataRecv(const esp_now_recv_info *recv_info, const uint8_t *incomingData, int len);
void applyEffect(); // Main function to dispatch to the correct effect handler
void effectSolid(); // Implements the solid color effect
void effectRainbow(); // Implements the rainbow effect
void effectFade(); // Implements the fading effect
void effectStrobe(); // Implements the strobe effect
void effectPulse(); // Implements the pulse effect

/**
 * @brief Callback function executed when ESP-NOW data is received.
 * This function runs in an ISR context, so keep it short and avoid complex operations.
 * @param recv_info Pointer to structure containing sender MAC address and other info.
 * @param incomingData Pointer to the received data payload.
 * @param len Length of the received data.
 */
void OnDataRecv(const esp_now_recv_info *recv_info, const uint8_t *incomingData, int len) {
    // Check if the received data length matches our expected command structure size.
    if (len == sizeof(led_command_t)) {
        // Copy the incoming data into our volatile receivedCommand structure.
        memcpy((void*)&receivedCommand, incomingData, sizeof(receivedCommand));
        newCommandReceived = true; // Set the flag to true to signal new data in the main loop.
    } else {
        // Log an error if the data length is unexpected, which indicates a potential mismatch
        // between transmitter and receiver data structures.
        Serial.print("\n--- ESP-NOW Packet Error (ISR) ---");
        Serial.print("\nReceived data of unexpected length: ");
        Serial.println(len);
    }
}

/**
 * @brief Initializes the ESP32, Serial communication, FastLED, and ESP-NOW.
 * Also runs an initial LED test pattern.
 */
void setup() {
    Serial.begin(115200); // Start serial communication for debugging
    Serial.println("\nESP-NOW Receiver Starting...");

    // Set WiFi and ESP-NOW log levels to suppress verbose output.
    // ESP_LOG_WARN shows only warnings and errors, keeping the serial output cleaner.
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("esp_now", ESP_LOG_WARN);

    // --- FastLED Initialization and Test Pattern ---
    // Add the LED strip to FastLED, specifying type, data pin, and color order.
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(50); // Set an initial brightness for the test pattern

    Serial.println("Running initial FastLED test pattern...");
    // Cycle through basic colors to confirm LED functionality.
    fill_solid(leds, NUM_LEDS, CRGB::Red); FastLED.show(); Serial.println("LEDs should be RED now."); delay(1000);
    fill_solid(leds, NUM_LEDS, CRGB::Green); FastLED.show(); Serial.println("LEDs should be GREEN now."); delay(1000);
    fill_solid(leds, NUM_LEDS, CRGB::Blue); FastLED.show(); Serial.println("LEDs should be BLUE now."); delay(1000);
    fill_solid(leds, NUM_LEDS, CRGB::White); FastLED.show(); Serial.println("LEDs should be WHITE now."); delay(1000);
    
    // Turn off LEDs after the test pattern.
    fill_solid(leds, NUM_LEDS, CRGB::Black); FastLED.show();
    Serial.println("LEDs turned OFF after test pattern. Waiting for 1 second to confirm.");
    delay(1000);

    // --- End of FastLED Test Pattern ---

    // Initialize WiFi in Station mode. ESP-NOW requires WiFi to be initialized.
    WiFi.mode(WIFI_STA);
    Serial.print("ESP32 MAC Address: ");
    Serial.println(WiFi.macAddress()); // Print the MAC address for debugging/peer configuration

    // Initialize ESP-NOW. Check for success.
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return; // Halt if ESP-NOW initialization fails
    }
    Serial.println("ESP-NOW Initialized");

    // Register the ESP-NOW receive callback function.
    esp_now_register_recv_cb(OnDataRecv);

    Serial.println("ESP-NOW Receiver Ready! Waiting for commands...");
}

/**
 * @brief Main loop function, continuously runs after setup.
 * Handles processing of new ESP-NOW commands and updating LED effects.
 */
void loop() {
    bool shouldUpdateLeds = false; // Flag to determine if FastLED.show() should be called

    // Check if a new command has been received from the transmitter.
    if (newCommandReceived) {
        newCommandReceived = false; // Reset the flag immediately to avoid re-processing the same command.
        
        // Update global state variables based on the new command.
        currentColor = CRGB(receivedCommand.red, receivedCommand.green, receivedCommand.blue);
        currentEffect = receivedCommand.effect;
        currentSpeed = receivedCommand.speed;

        // Reset effect-specific states to ensure effects start fresh with new parameters.
        rainbowHue = 0; // Reset rainbow phase
        strobeState = false; // Reset strobe state to off
        fadeStartTime = millis(); // Reset fade timer to current time
        fadingIn = true; // Start fade by fading in to the new color
        fadeStartColor = CRGB::Black; // Always start fade from black
        fadeTargetColor = currentColor; // Target is the new received color

        Serial.print("\n--- Processing New Command in Loop ---");
        Serial.printf("Applying Color: R:%d G:%d B:%d\n", currentColor.r, currentColor.g, currentColor.b);
        Serial.printf("Applying Brightness (1-100): %d\n", receivedCommand.brightness);
        Serial.printf("Applying Effect: %d, Speed: %d\n", currentEffect, currentSpeed);

        shouldUpdateLeds = true; // A new command always triggers an immediate LED update.
    }

    // If no new command, but an effect is active (not 'Solid'), check if it's time for an update.
    // 'Solid' effect only needs to update when a new command changes its color/brightness.
    if (!shouldUpdateLeds && currentEffect != 0 && (millis() - lastLedUpdateTime >= LED_UPDATE_INTERVAL_MS)) {
        shouldUpdateLeds = true; // Time to update the effect animation.
    }

    // If LEDs need to be updated (either due to new command or effect timing), then proceed.
    if (shouldUpdateLeds) {
        lastLedUpdateTime = millis(); // Record the time of this update.
        FastLED.clear(); // Clear the LED buffer to prevent artifacts from previous frames.
        
        // Map the 1-100 brightness value from the transmitter to FastLED's 0-255 scale.
        uint8_t fastledBrightness = map(receivedCommand.brightness, 1, 100, 0, 255);
        FastLED.setBrightness(fastledBrightness); // Apply the overall brightness.

        applyEffect(); // Call the function to render the current effect.
        FastLED.show(); // Push the LED data to the physical LED matrix.
        // Serial.println("LEDs updated based on received command or effect timer."); // Uncomment for detailed logging
    }

    delay(10); // Small delay to yield CPU time to other tasks and prevent watchdog timer resets.
}

/**
 * @brief Dispatches to the appropriate effect function based on `currentEffect`.
 */
void applyEffect() {
    switch (currentEffect) {
        case 0: // Solid color effect
            effectSolid();
            break;
        case 1: // Rainbow effect
            effectRainbow();
            break;
        case 2: // Fade effect (fades color in and out)
            effectFade();
            break;
        case 3: // Strobe effect (flashes color on and off)
            effectStrobe();
            break;
        case 4: // Pulse effect (smoothly pulses brightness)
            effectPulse();
            break;
        default: // Fallback to solid color if an unknown effect ID is received
            effectSolid();
            break;
    }
}

// --- LED Effect Implementations ---

/**
 * @brief Fills the entire LED strip with the `currentColor`.
 */
void effectSolid() {
    fill_solid(leds, NUM_LEDS, currentColor);
}

/**
 * @brief Generates a moving rainbow pattern across the LEDs.
 * The `currentSpeed` controls how fast the rainbow shifts.
 */
void effectRainbow() {
    // Calculate hue position based on continuous time (not frame-based)
    // This ensures smooth movement regardless of update rate
    static uint8_t hueOffset = 0;
    uint16_t speedFactor = map(currentSpeed, 1, 100, 300, 20); // Higher = slower
    
    // Smooth continuous movement using elapsed time
    hueOffset = (millis() / speedFactor) % 256;
    
    // Apply rainbow to all LEDs
    for (int i = 0; i < NUM_LEDS; i++) {
        // Classic rainbow spread across all LEDs
        leds[i] = CHSV(hueOffset + (i * 256 / NUM_LEDS), 255, 255);
    }
    
    // Optional: Add this if you want to see the actual hue progression in serial
    // static uint8_t lastHue = 0;
    // if (hueOffset != lastHue) {
    //     Serial.printf("Hue: %d\n", hueOffset);
    //     lastHue = hueOffset;
    // }
}

/**
 * @brief Fades the `currentColor` in and out from black.
 * The `currentSpeed` controls the duration of one fade cycle.
 */
void effectFade() {
    unsigned long fadeDuration = map(currentSpeed, 1, 100, 5000, 500);
    unsigned long currentTime = millis();
    unsigned long elapsedTime = currentTime - fadeStartTime;

    if (elapsedTime >= fadeDuration) {
        fadingIn = !fadingIn;
        fadeStartTime = currentTime;
        fadeStartColor = fadingIn ? CRGB::Black : currentColor;
        fadeTargetColor = fadingIn ? currentColor : CRGB::Black;
        elapsedTime = 0;
    }

    float t = (float)elapsedTime / fadeDuration;
    t = min(t, 1.0f); // Ensure t doesn't exceed 1.0
    
    // Proper CRGB interpolation using FastLED's blend function
    CRGB interpolatedColor = blend(fadeStartColor, fadeTargetColor, (uint8_t)(t * 255));
    fill_solid(leds, NUM_LEDS, interpolatedColor);
}

/**
 * @brief Implements a strobe light effect with `currentColor`.
 * The `currentSpeed` controls the flash rate.
 */
void effectStrobe() {
    unsigned long strobeDelay = map(currentSpeed, 1, 100, 1000, 50);
    if (millis() - lastEffectRunTime >= strobeDelay) {
        lastEffectRunTime = millis();
        strobeState = !strobeState;
        fill_solid(leds, NUM_LEDS, strobeState ? currentColor : CRGB::Black);
    }
}


/**
 * @brief Smoothly pulses the brightness of the `currentColor`.
 * The `currentSpeed` controls the period of the pulse.
 */
void effectPulse() {
    unsigned long pulsePeriod = map(currentSpeed, 1, 100, 10000, 1000);
    float phase = (float)(millis() % pulsePeriod) / pulsePeriod;
    float brightnessFactor = (sin(phase * TWO_PI) + 1.0) / 2.0;
    
    // Correct CRGB brightness scaling
    CRGB pulsedColor = currentColor;
    pulsedColor.nscale8_video(brightnessFactor * 255);
    fill_solid(leds, NUM_LEDS, pulsedColor);
}
