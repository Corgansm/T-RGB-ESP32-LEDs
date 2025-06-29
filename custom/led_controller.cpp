/**
 * @file      led_controller.ino
 * @brief     ESP-NOW LED Controller for Circular Display - Stacked Layout
 * @author    Your Name
 * @license   MIT
 */

#include <LilyGo_RGBPanel.h>
#include <LV_Helper.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_log.h> // Include for esp_log_level_set

// Display dimensions
#define DISPLAY_DIAMETER 480
#define DISPLAY_RADIUS 240

// LED Configuration
#define DEFAULT_BRIGHTNESS 50

// ESP-NOW Configuration
#define ESPNOW_CHANNEL 1
// Receiver's MAC address (from your previous input)
uint8_t broadcastAddress[] = {0xEC, 0xE3, 0x34, 0x46, 0x00, 0x1C};

// LED Control Structure
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

led_command_t ledCommand;
uint32_t lastSendTime = 0;
const uint32_t sendInterval = 100; // ms between updates (now only for checking if a send is needed)

LilyGo_RGBPanel panel;

// LVGL Objects
static lv_obj_t *brightnessSlider;
static lv_obj_t *colorPicker;
static lv_obj_t *whiteSlider;
static lv_obj_t *warmWhiteSlider;
static lv_obj_t *effectDropdown;
static lv_obj_t *speedSlider;
static lv_obj_t *statusLabel;
static lv_obj_t *tv; // Tileview instead of tabview

// Function prototypes
void createUI();
void createColorPage(lv_obj_t *parent);
void createEffectsPage(lv_obj_t *parent);
void createWhiteControl(lv_obj_t *parent, const char *label, lv_obj_t **slider, uint8_t value, int y_offset);
void colorPickerEvent(lv_event_t *e);
void brightnessSliderEvent(lv_event_t *e);
void whiteSliderEvent(lv_event_t *e);
void effectDropdownEvent(lv_event_t *e);
void speedSliderEvent(lv_event_t *e);
void updateStatus(const char *message);
void sendCommand(); // Function to send ESP-NOW command

// Callback function for ESP-NOW send status
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("ESP-NOW Send Success");
        updateStatus("Sent: OK");
    } else {
        Serial.print("ESP-NOW Send Failed: ");
        Serial.println(status);
        updateStatus("Sent: Failed!");
    }
}

void setup() {
    Serial.begin(115200);

    // Initialize the circular display
    if (!panel.begin(LILYGO_T_RGB_2_8_INCHES)) {
        Serial.println("Error initializing circular display");
        while (1);
    }

    // Initialize LVGL
    beginLvglHelper(panel);

    // Set default LED values
    memset(&ledCommand, 0, sizeof(ledCommand));
    ledCommand.brightness = DEFAULT_BRIGHTNESS;
    ledCommand.effect = 0;
    ledCommand.speed = 50;
    ledCommand.red = 255; // Default to red color
    ledCommand.green = 0;
    ledCommand.blue = 0;
    ledCommand.white = 0;
    ledCommand.warmWhite = 0;

    // Initialize ESP-NOW
    WiFi.mode(WIFI_STA);
    // Set WiFi log level to suppress verbose output
    // ESP_LOG_NONE will suppress almost all WiFi related logs
    // ESP_LOG_WARN will show warnings but suppress info/debug
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("esp_now", ESP_LOG_WARN); // Also reduce ESP-NOW specific logs

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    // Register the ESP-NOW send callback
    esp_now_register_send_cb(OnDataSent);

    // Add peer (the receiver's MAC address)
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = false; // Assuming no encryption
    if (esp_now_add_peer(&peerInfo) != ESP_OK){
        Serial.println("Failed to add peer");
        return;
    }
    Serial.println("Peer Added");

    // Create optimized UI
    createUI();

    // Set backlight
    panel.setBrightness(10);

    // Force an initial send to ensure receiver gets data immediately
    // The OnDataSent callback will update the status label
    sendCommand();
    updateStatus("Initialized. Sending...");
}

void loop() {
    lv_timer_handler();
    
    // Only attempt to send command if values have changed
    // The actual sending logic is now inside sendCommand()
    // and relies on memcmp for efficiency.
    // The sendInterval is still used to periodically re-send the current state
    // if no changes are made, for robustness.
    static uint32_t lastLoopSendCheck = 0;
    if (millis() - lastLoopSendCheck > sendInterval) {
        lastLoopSendCheck = millis();
        sendCommand(); // This will only send if values changed or if it's a periodic re-send
    }
    
    delay(5);
}

void createUI() {
    // Main container with circular mask
    lv_obj_t *cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, DISPLAY_DIAMETER, DISPLAY_DIAMETER);
    lv_obj_set_style_radius(cont, DISPLAY_RADIUS, 0);
    lv_obj_set_style_clip_corner(cont, true, 0);
    lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
    lv_obj_center(cont);

    // Create tileview for swipe navigation (hidden tabs)
    tv = lv_tileview_create(cont);
    lv_obj_set_size(tv, DISPLAY_DIAMETER-20, DISPLAY_DIAMETER-20);
    lv_obj_center(tv);
    lv_obj_set_style_bg_opa(tv, LV_OPA_0, 0);
    lv_obj_set_style_border_width(tv, 0, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    // Create tiles (pages)
    lv_obj_t *t1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_HOR);
    lv_obj_t *t2 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
    
    // Configure tiles
    lv_obj_set_style_pad_all(t1, 5, 0);
    lv_obj_set_style_pad_all(t2, 5, 0);
    lv_obj_set_style_bg_opa(t1, LV_OPA_0, 0);
    lv_obj_set_style_bg_opa(t2, LV_OPA_0, 0);

    // Create content for each tile
    createColorPage(t1);
    createEffectsPage(t2);
}

void createColorPage(lv_obj_t *parent) {
    // Stacked color wheel and brightness slider
    colorPicker = lv_colorwheel_create(parent, true);
    lv_obj_set_size(colorPicker, DISPLAY_RADIUS-80, DISPLAY_RADIUS-80);
    lv_obj_set_style_arc_width(colorPicker, 15, LV_PART_MAIN);
    lv_obj_align(colorPicker, LV_ALIGN_CENTER, 0, -60);
    lv_obj_add_event_cb(colorPicker, colorPickerEvent, LV_EVENT_VALUE_CHANGED, NULL);

    // Brightness slider stacked below color picker
    lv_obj_t *brightnessCont = lv_obj_create(parent);
    lv_obj_set_size(brightnessCont, DISPLAY_RADIUS+50, 60);
    lv_obj_align(brightnessCont, LV_ALIGN_CENTER, 0, DISPLAY_RADIUS/2-40);
    lv_obj_set_flex_flow(brightnessCont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brightnessCont, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(brightnessCont, LV_OPA_0, 0);

    lv_obj_t *brightnessText = lv_label_create(brightnessCont);
    lv_label_set_text(brightnessText, "Bright:");
    lv_obj_set_style_text_color(brightnessText, lv_color_white(), 0);

    brightnessSlider = lv_slider_create(brightnessCont);
    lv_slider_set_range(brightnessSlider, 1, 100);
    lv_slider_set_value(brightnessSlider, DEFAULT_BRIGHTNESS, LV_ANIM_OFF);
    lv_obj_set_width(brightnessSlider, 120);
    lv_obj_add_event_cb(brightnessSlider, brightnessSliderEvent, LV_EVENT_VALUE_CHANGED, NULL);

    // White controls at bottom
    createWhiteControl(parent, "White", &whiteSlider, 0, DISPLAY_RADIUS/2+10);
    createWhiteControl(parent, "Warm", &warmWhiteSlider, 0, DISPLAY_RADIUS/2+50);
}

void createEffectsPage(lv_obj_t *parent) {
    // Effect dropdown centered
    effectDropdown = lv_dropdown_create(parent);
    lv_dropdown_set_options(effectDropdown, "Solid\nRainbow\nFade\nStrobe\nPulse");
    lv_obj_set_width(effectDropdown, DISPLAY_RADIUS+50);
    lv_obj_align(effectDropdown, LV_ALIGN_CENTER, 0, -50);
    lv_obj_add_event_cb(effectDropdown, effectDropdownEvent, LV_EVENT_VALUE_CHANGED, NULL);

    // Speed control below
    lv_obj_t *speedCont = lv_obj_create(parent);
    lv_obj_set_size(speedCont, DISPLAY_RADIUS+50, 50);
    lv_obj_align(speedCont, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_flex_flow(speedCont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(speedCont, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(speedCont, LV_OPA_0, 0);

    lv_obj_t *speedText = lv_label_create(speedCont);
    lv_label_set_text(speedText, "Speed:");
    lv_obj_set_style_text_color(speedText, lv_color_white(), 0);

    speedSlider = lv_slider_create(speedCont);
    lv_slider_set_range(speedSlider, 1, 100);
    lv_slider_set_value(speedSlider, 50, LV_ANIM_OFF);
    lv_obj_set_width(speedSlider, 120);
    lv_obj_add_event_cb(speedSlider, speedSliderEvent, LV_EVENT_VALUE_CHANGED, NULL);

    // Status at bottom
    statusLabel = lv_label_create(parent);
    lv_label_set_text(statusLabel, "Ready");
    lv_obj_set_style_text_color(statusLabel, lv_color_white(), 0);
    lv_obj_align(statusLabel, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void createWhiteControl(lv_obj_t *parent, const char *label, lv_obj_t **slider, uint8_t value, int y_offset) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, DISPLAY_RADIUS+50, 40);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, y_offset);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cont, LV_OPA_0, 0);

    lv_obj_t *label_obj = lv_label_create(cont);
    lv_label_set_text(label_obj, label);
    lv_obj_set_style_text_color(label_obj, lv_color_white(), 0);

    *slider = lv_slider_create(cont);
    lv_slider_set_range(*slider, 0, 255);
    lv_slider_set_value(*slider, value, LV_ANIM_OFF);
    lv_obj_set_width(*slider, 120);
    lv_obj_add_event_cb(*slider, whiteSliderEvent, LV_EVENT_VALUE_CHANGED, NULL);
}

// Event handlers
void colorPickerEvent(lv_event_t *e) {
    lv_color_t color = lv_colorwheel_get_rgb(colorPicker);
    ledCommand.red = color.ch.red;
    ledCommand.green = color.ch.green;
    ledCommand.blue = color.ch.blue;
    updateStatus("Color updated");
    sendCommand(); // Send immediately when color changes
}

void brightnessSliderEvent(lv_event_t *e) {
    ledCommand.brightness = lv_slider_get_value(brightnessSlider);
    updateStatus("Brightness updated");
    sendCommand(); // Send immediately when brightness changes
}

void whiteSliderEvent(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    uint8_t value = lv_slider_get_value(slider);
    
    if (slider == whiteSlider) {
        ledCommand.white = value;
        updateStatus("White updated");
    } else {
        ledCommand.warmWhite = value;
        updateStatus("Warm white updated");
    }
    sendCommand(); // Send immediately when white/warm white changes
}

void effectDropdownEvent(lv_event_t *e) {
    ledCommand.effect = lv_dropdown_get_selected(effectDropdown);
    updateStatus("Effect updated");
    sendCommand(); // Send immediately when effect changes
}

void speedSliderEvent(lv_event_t *e) {
    ledCommand.speed = lv_slider_get_value(speedSlider);
    updateStatus("Speed updated");
    sendCommand(); // Send immediately when speed changes
}

void updateStatus(const char *message) {
    lv_label_set_text(statusLabel, message);
}

void sendCommand() {
    static led_command_t lastCommandSent; // Tracks the *last successfully sent* command
    
    // Only send if the current ledCommand is different from the last one sent
    if (memcmp(&lastCommandSent, &ledCommand, sizeof(ledCommand)) != 0) {
        esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&ledCommand, sizeof(ledCommand));
        
        // Update lastCommandSent only if the send attempt was successful
        if (result == ESP_OK) {
            memcpy(&lastCommandSent, &ledCommand, sizeof(ledCommand));
            // Status will be updated by OnDataSent callback
        } else {
            Serial.print("ESP-NOW Send Attempt Failed (Error ");
            Serial.print(result);
            Serial.println("). Will retry on next change/interval.");
            updateStatus("Send Failed!"); // Immediate feedback on UI
        }
    }
    // If values haven't changed, no send attempt is made, preventing spam.
    // The loop() will periodically call sendCommand() which will then check memcmp again.
}
