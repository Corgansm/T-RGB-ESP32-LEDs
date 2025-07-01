/**
 * @file      led_controller.cpp
 * @brief     ESP-NOW LED Controller for Circular Display - Enhanced & Fixed
 * @author    Enhanced Version
 * @license   MIT
 * @version   2.0
 */

#include <LilyGo_RGBPanel.h>
#include <LV_Helper.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_log.h>

// =============================================================================
// CONFIGURATION
// =============================================================================
#define DISPLAY_DIAMETER 480
#define DISPLAY_RADIUS 240
#define DEFAULT_BRIGHTNESS 16
#define ESPNOW_CHANNEL 1

// Receiver's MAC address - UPDATE THIS TO MATCH YOUR RECEIVER
uint8_t receiverAddress[] = {0x6C, 0xC8, 0x40, 0x88, 0x58, 0xA0};

// =============================================================================
// DATA STRUCTURES
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

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================
led_command_t ledCommand;
LilyGo_RGBPanel panel;

// Sleep
bool displaySleeping = false;
int displayBrightness = 16;

// Led Status
bool ledsOn = true;

// Communication tracking
bool lastSendSuccess = false;
uint32_t commandsSent = 0;
uint32_t requestsReceived = 0;
uint32_t lastHeartbeat = 0;
const uint32_t HEARTBEAT_INTERVAL = 5000; // Send heartbeat every 5 seconds

// UI Objects
static lv_obj_t *brightnessSlider;
static lv_obj_t *colorPicker;
static lv_obj_t *whiteSlider;
static lv_obj_t *warmWhiteSlider;
static lv_obj_t *effectDropdown;
static lv_obj_t *speedSlider;
static lv_obj_t *statusLabel;
static lv_obj_t *statsLabel;
static lv_obj_t *tv;
static lv_obj_t *label;
static lv_obj_t *label_level;

// =============================================================================
// FUNCTION PROTOTYPES
// =============================================================================
void initializeESPNOW();
void createUI();
void createColorPage(lv_obj_t *parent);
void createEffectsPage(lv_obj_t *parent);
void createDisplayPage(lv_obj_t *parent);
void createWhiteControl(lv_obj_t *parent, const char *label, lv_obj_t **slider, uint8_t value, int y_offset);
void updateStatus(const char *message, bool isError = false);
void updateStats();
void sendCommand();
void sendHeartbeat();

// Event handlers
void colorPickerEvent(lv_event_t *e);
void brightnessSliderEvent(lv_event_t *e);
void whiteSliderEvent(lv_event_t *e);
void effectDropdownEvent(lv_event_t *e);
void speedSliderEvent(lv_event_t *e);
void sleepButtonEvent(lv_event_t *e);
void backlightSliderEvent(lv_event_t *e);
void ledToggleButtonEvent(lv_event_t *e);

// =============================================================================
// ESP-NOW CALLBACKS
// =============================================================================
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    lastSendSuccess = (status == ESP_NOW_SEND_SUCCESS);
    
    if (lastSendSuccess) {
        Serial.println("✓ ESP-NOW Send Success");
        updateStatus("Connected", false);
        commandsSent++;
    } else {
        Serial.printf("✗ ESP-NOW Send Failed: %d\n", status);
        updateStatus("Send Failed!", true);
    }
    updateStats();
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    static uint32_t lastRequest = 0;
    
    // Debounce requests
    if (millis() - lastRequest < 200) return;
    lastRequest = millis();

    Serial.printf("\n📨 Received %d bytes from: ", len);
    for(int i = 0; i < 6; i++) {
        Serial.printf("%02X", mac[i]);
        if(i < 5) Serial.print(":");
    }
    Serial.println();
    
    if (len == sizeof(color_request_t)) {
        color_request_t req;
        memcpy(&req, incomingData, len);
        
        Serial.printf("Request Type: %d, From Receiver: %d\n", req.requestType, req.fromReceiver);
        
        if (req.requestType == 1 && req.fromReceiver == 1) {
            Serial.println("🎯 Valid color request received - responding immediately");
            requestsReceived++;
            updateStatus("Request received", false);
            updateStats();
            sendCommand();
        }
    } else {
        Serial.printf("⚠️ Unexpected data length: %d (expected %d)\n", len, sizeof(color_request_t));
    }
}

// =============================================================================
// ESP-NOW INITIALIZATION
// =============================================================================
void initializeESPNOW() {
    // Initialize WiFi
    WiFi.mode(WIFI_STA);
    
    // Suppress verbose WiFi logs
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("esp_now", ESP_LOG_WARN);
    
    delay(100);
    
    Serial.println("\n🔧 Initializing ESP-NOW...");
    Serial.printf("📡 Controller MAC: %s\n", WiFi.macAddress().c_str());

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW initialization failed!");
        updateStatus("ESP-NOW Init Failed!", true);
        return;
    }

    // Register callbacks
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    // Add peer (receiver)
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, receiverAddress, 6);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("❌ Failed to add receiver peer");
        updateStatus("Peer Add Failed!", true);
    } else {
        Serial.printf("✅ Added receiver peer: ");
        for(int i = 0; i < 6; i++) {
            Serial.printf("%02X", receiverAddress[i]);
            if(i < 5) Serial.print(":");
        }
        Serial.println();
        updateStatus("ESP-NOW Ready", false);
    }
}

// =============================================================================
// MAIN SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n🚀 ESP-NOW LED Controller Starting...");
    Serial.println("==========================================");

    // Initialize display
    if (!panel.begin(LILYGO_T_RGB_2_8_INCHES)) {
        Serial.println("❌ Display initialization failed!");
        while (1);
    }
    Serial.println("✅ Display initialized");

    // Initialize LVGL
    beginLvglHelper(panel);
    Serial.println("✅ LVGL initialized");

    // Set default LED values
    memset(&ledCommand, 0, sizeof(ledCommand));
    ledCommand.brightness = DEFAULT_BRIGHTNESS;
    ledCommand.effect = 0;
    ledCommand.speed = 50;
    ledCommand.red = 255;
    ledCommand.green = 0;
    ledCommand.blue = 0;
    ledCommand.white = 0;
    ledCommand.warmWhite = 0;

    // Initialize ESP-NOW
    initializeESPNOW();

    // Create UI
    createUI();

    // Set display brightness
    panel.setBrightness(10);

    // Send initial command
    updateStatus("Initializing...", false);
    delay(1000); // Give receiver time to initialize
    sendCommand();
    
    Serial.println("🎮 Controller ready for use!");
    Serial.println("==========================================\n");
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
    lv_timer_handler();  // LVGL's periodic update
    delay(5);            // Required LVGL delay
    
    
    // Send periodic heartbeat to maintain connection
    if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
        sendHeartbeat();
        lastHeartbeat = millis();
    }
    
    if (displaySleeping) {
        // Manually read touch here
        int16_t touchX, touchY;
        bool touched = panel.getPoint(&touchX, &touchY);  // Replace with your panel’s touch read method
        
        if (touched) {
            panel.setBrightness(displayBrightness);  // Or panel.wakeup()
            displaySleeping = false;
            updateStatus("Display awake", false);
            Serial.printf("Touch at (%d, %d) - Waking display\n", touchX, touchY);
        }
    }

    
    delay(10);
}

// =============================================================================
// UI CREATION
// =============================================================================
void createUI() {
    // Main container with circular mask
    lv_obj_t *cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, DISPLAY_DIAMETER, DISPLAY_DIAMETER);
    lv_obj_set_style_radius(cont, DISPLAY_RADIUS, 0);
    lv_obj_set_style_clip_corner(cont, true, 0);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(cont, 3, 0);
    lv_obj_set_style_border_color(cont, lv_color_hex(0x4a90e2), 0);
    lv_obj_center(cont);

    // Create tileview for swipe navigation
    tv = lv_tileview_create(cont);
    lv_obj_set_size(tv, DISPLAY_DIAMETER-20, DISPLAY_DIAMETER-20);
    lv_obj_center(tv);
    lv_obj_set_style_bg_opa(tv, LV_OPA_0, 0);
    lv_obj_set_style_border_width(tv, 0, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    // Create tiles
    lv_obj_t *colorTile = lv_tileview_add_tile(tv, 0, 0, LV_DIR_HOR);
    lv_obj_t *effectsTile = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
    lv_obj_t *DisplayTile = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);
    
    lv_obj_set_style_pad_all(colorTile, 8, 0);
    lv_obj_set_style_pad_all(effectsTile, 8, 0);
    lv_obj_set_style_bg_opa(colorTile, LV_OPA_0, 0);
    lv_obj_set_style_bg_opa(effectsTile, LV_OPA_0, 0);
    lv_obj_set_style_pad_all(DisplayTile, 8, 0);
    lv_obj_set_style_bg_opa(DisplayTile, LV_OPA_0, 0);

    createColorPage(colorTile);
    createEffectsPage(effectsTile);
    createDisplayPage(DisplayTile);
}

void createColorPage(lv_obj_t *parent) {
    // Title
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Color Control");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Color wheel
    colorPicker = lv_colorwheel_create(parent, true);
    lv_obj_set_size(colorPicker, DISPLAY_RADIUS-60, DISPLAY_RADIUS-60);
    lv_obj_set_style_arc_width(colorPicker, 20, LV_PART_MAIN);
    lv_obj_align(colorPicker, LV_ALIGN_CENTER, 0, -90);
    lv_obj_add_event_cb(colorPicker, colorPickerEvent, LV_EVENT_VALUE_CHANGED, NULL);
    
    lv_colorwheel_set_mode(colorPicker, LV_COLORWHEEL_MODE_HUE);
    lv_colorwheel_set_mode_fixed(colorPicker, true);

    // Brightness control
    lv_obj_t *brightnessCont = lv_obj_create(parent);
    lv_obj_set_size(brightnessCont, DISPLAY_RADIUS+70, 50);
    lv_obj_align(brightnessCont, LV_ALIGN_CENTER, 0, DISPLAY_RADIUS/2-60);
    lv_obj_set_flex_flow(brightnessCont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brightnessCont, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(brightnessCont, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_radius(brightnessCont, 10, 0);

    lv_obj_t *brightnessIcon = lv_label_create(brightnessCont);
    lv_label_set_text(brightnessIcon, "Brightness");
    lv_obj_set_style_text_color(brightnessIcon, lv_color_white(), 0);

    brightnessSlider = lv_slider_create(brightnessCont);
    lv_slider_set_range(brightnessSlider, 1, 100);
    lv_slider_set_value(brightnessSlider, DEFAULT_BRIGHTNESS, LV_ANIM_OFF);
    lv_obj_set_width(brightnessSlider, 140);
    lv_obj_set_style_bg_color(brightnessSlider, lv_color_hex(0x4a4a4a), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightnessSlider, lv_color_hex(0x4a90e2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightnessSlider, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(brightnessSlider, brightnessSliderEvent, LV_EVENT_VALUE_CHANGED, NULL);

    // White controls
    createWhiteControl(parent, "White", &whiteSlider, 0, DISPLAY_RADIUS/2-10);
    createWhiteControl(parent, "Warm", &warmWhiteSlider, 0, DISPLAY_RADIUS/2+30);
}

void createEffectsPage(lv_obj_t *parent) {
    // Title
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Effects & Status");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *ledToggleBtn = lv_btn_create(parent);
    lv_obj_align(ledToggleBtn, LV_ALIGN_CENTER, 0, -150);
    lv_obj_set_style_bg_color(ledToggleBtn, lv_color_hex(0x00FF00), LV_PART_MAIN);  // Initial green
    lv_obj_t *btnLabel = lv_label_create(ledToggleBtn);
    lv_label_set_text(btnLabel, "LEDs ON");
    lv_obj_add_event_cb(ledToggleBtn, ledToggleButtonEvent, LV_EVENT_CLICKED, btnLabel);

    // Effect dropdown
    effectDropdown = lv_dropdown_create(parent);
    lv_dropdown_set_options(effectDropdown, "Solid\nRainbow\nFade\nStrobe\nPulse\nSparkle\nWave");
    lv_obj_set_width(effectDropdown, DISPLAY_RADIUS+70);
    lv_obj_align(effectDropdown, LV_ALIGN_CENTER, 0, -80);
    lv_obj_set_style_bg_color(effectDropdown, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_text_color(effectDropdown, lv_color_white(), 0);
    lv_obj_add_event_cb(effectDropdown, effectDropdownEvent, LV_EVENT_VALUE_CHANGED, NULL);

    // Speed control
    lv_obj_t *speedCont = lv_obj_create(parent);
    lv_obj_set_size(speedCont, DISPLAY_RADIUS+70, 50);
    lv_obj_align(speedCont, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_flex_flow(speedCont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(speedCont, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(speedCont, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_radius(speedCont, 10, 0);

    lv_obj_t *speedIcon = lv_label_create(speedCont);
    lv_label_set_text(speedIcon, "Speed");
    lv_obj_set_style_text_color(speedIcon, lv_color_white(), 0);

    speedSlider = lv_slider_create(speedCont);
    lv_slider_set_range(speedSlider, 1, 100);
    lv_slider_set_value(speedSlider, 50, LV_ANIM_OFF);
    lv_obj_set_width(speedSlider, 140);
    lv_obj_set_style_bg_color(speedSlider, lv_color_hex(0x4a4a4a), LV_PART_MAIN);
    lv_obj_set_style_bg_color(speedSlider, lv_color_hex(0x4a90e2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(speedSlider, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(speedSlider, speedSliderEvent, LV_EVENT_VALUE_CHANGED, NULL);

    // Status display
    lv_obj_t *statusCont = lv_obj_create(parent);
    lv_obj_set_size(statusCont, DISPLAY_RADIUS+70, 80);
    lv_obj_align(statusCont, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_bg_color(statusCont, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_radius(statusCont, 10, 0);

    statusLabel = lv_label_create(statusCont);
    lv_label_set_text(statusLabel, "Initializing...");
    lv_obj_set_style_text_color(statusLabel, lv_color_white(), 0);
    lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(statusLabel, LV_ALIGN_TOP_MID, 0, 10);

    statsLabel = lv_label_create(statusCont);
    lv_label_set_text(statsLabel, "Sent: 0 | Requests: 0");
    lv_obj_set_style_text_color(statsLabel, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(statsLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(statsLabel, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void createWhiteControl(lv_obj_t *parent, const char *label, lv_obj_t **slider, uint8_t value, int y_offset) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, DISPLAY_RADIUS+70, 40);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, y_offset);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_radius(cont, 10, 0);

    lv_obj_t *label_obj = lv_label_create(cont);
    lv_label_set_text(label_obj, label);
    lv_obj_set_style_text_color(label_obj, lv_color_white(), 0);

    *slider = lv_slider_create(cont);
    lv_slider_set_range(*slider, 0, 255);
    lv_slider_set_value(*slider, value, LV_ANIM_OFF);
    lv_obj_set_width(*slider, 120);
    lv_obj_set_style_bg_color(*slider, lv_color_hex(0x4a4a4a), LV_PART_MAIN);
    lv_obj_set_style_bg_color(*slider, lv_color_hex(0x4a90e2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(*slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(*slider, whiteSliderEvent, LV_EVENT_VALUE_CHANGED, NULL);
}

void createDisplayPage(lv_obj_t *parent) {
    // Title
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Backlight and Sleep");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Backlight slider
    lv_obj_t *backlightSlider = lv_slider_create(parent);
    lv_slider_set_range(backlightSlider, 1, 16);
    lv_slider_set_value(backlightSlider, DEFAULT_BRIGHTNESS, LV_ANIM_OFF);
    lv_obj_align(backlightSlider, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_add_event_cb(backlightSlider, backlightSliderEvent, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *backlightLabel = lv_label_create(parent);
    lv_label_set_text(backlightLabel, "Backlight");
    lv_obj_align_to(backlightLabel, backlightSlider, LV_ALIGN_OUT_TOP_MID, 0, -5);

    // Sleep button
    lv_obj_t *sleepBtn = lv_btn_create(parent);
    lv_obj_align(sleepBtn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_t *btnLabel = lv_label_create(sleepBtn);
    lv_label_set_text(btnLabel, "Sleep");
    lv_obj_add_event_cb(sleepBtn, sleepButtonEvent, LV_EVENT_CLICKED, NULL);
}

// =============================================================================
// EVENT HANDLERS
// =============================================================================

void ledToggleButtonEvent(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    ledsOn = !ledsOn;

    if (ledsOn) {
        lv_label_set_text(label, "LEDs ON");
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x00FF00), LV_PART_MAIN);  // Green
        Serial.println("LEDs turned ON");
        ledCommand.brightness = displayBrightness;
        sendCommand();
    } else {
        lv_label_set_text(label, "LEDs OFF");
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF0000), LV_PART_MAIN);  // Red
        Serial.println("LEDs turned OFF");
        ledCommand.brightness = -255;
        sendCommand();
    }
}

void backlightSliderEvent(lv_event_t *e) {
    int brightness = lv_slider_get_value(lv_event_get_target(e));
    panel.setBrightness(brightness);  // Adjust backlight via panel
    displayBrightness = brightness;
    Serial.printf("Backlight set to %d\n", brightness);
}

void sleepButtonEvent(lv_event_t *e) {
    panel.setBrightness(0);  // Or panel.sleep()
    displaySleeping = true;
    updateStatus("Display asleep", false);
    Serial.println("Sleep button pressed: display off");
}

void colorPickerEvent(lv_event_t *e) {
    lv_color_t color = lv_colorwheel_get_rgb(colorPicker);
    ledCommand.red = color.ch.red;
    ledCommand.green = color.ch.green;
    ledCommand.blue = color.ch.blue;
    updateStatus("Color updated", false);
    sendCommand();
}

void brightnessSliderEvent(lv_event_t *e) {
    ledCommand.brightness = lv_slider_get_value(brightnessSlider);
    updateStatus("Brightness updated", false);
    sendCommand();
}

void whiteSliderEvent(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    uint8_t value = lv_slider_get_value(slider);
    
    if (slider == whiteSlider) {
        ledCommand.white = value;
        updateStatus("White updated", false);
    } else {
        ledCommand.warmWhite = value;
        updateStatus("Warm updated", false);
    }
    sendCommand();
}

void effectDropdownEvent(lv_event_t *e) {
    ledCommand.effect = lv_dropdown_get_selected(effectDropdown);
    updateStatus("Effect updated", false);
    sendCommand();
}

void speedSliderEvent(lv_event_t *e) {
    ledCommand.speed = lv_slider_get_value(speedSlider);
    updateStatus("Speed updated", false);
    sendCommand();
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================
void updateStatus(const char *message, bool isError) {
    if (statusLabel) {
        String statusText = isError ? "❌ " : "✅ ";
        statusText += message;
        lv_label_set_text(statusLabel, statusText.c_str());
        
        lv_color_t color = isError ? lv_color_hex(0xff4444) : lv_color_hex(0x44ff44);
        lv_obj_set_style_text_color(statusLabel, color, 0);
    }
    Serial.printf("Status: %s\n", message);
}

void updateStats() {
    if (statsLabel) {
        String stats = "Sent: " + String(commandsSent) + " | Requests: " + String(requestsReceived);
        lv_label_set_text(statsLabel, stats.c_str());
    }
}

void sendCommand() {
    static uint32_t lastSendAttempt = 0;
    
    // Rate limiting
    if (millis() - lastSendAttempt < 50) return;
    lastSendAttempt = millis();

    Serial.println("\n📤 Sending LED Command:");
    Serial.printf("  🎨 RGB: (%d, %d, %d)\n", ledCommand.red, ledCommand.green, ledCommand.blue);
    Serial.printf("  ⚪ White: %d, Warm: %d\n", ledCommand.white, ledCommand.warmWhite);
    Serial.printf("  ☀️ Brightness: %d%%\n", ledCommand.brightness);
    Serial.printf("  ✨ Effect: %d, Speed: %d\n", ledCommand.effect, ledCommand.speed);

    esp_err_t result = esp_now_send(receiverAddress, (uint8_t *)&ledCommand, sizeof(ledCommand));
    
    if (result == ESP_OK) {
        Serial.println("✅ Command queued for transmission");
    } else {
        Serial.printf("❌ Send failed with error: 0x%X\n", result);
        updateStatus("Send Error!", true);
    }
}

void sendHeartbeat() {
    // Send current state as heartbeat to maintain connection
    sendCommand();
}
