# ESP-NOW LED Controller & Receiver

This repository contains the code for an ESP-NOW based LED control system, comprising a transmitter (controller) and a receiver.

## Branch Information

Please note the following branch structure for the project files:

- `receiver.ino`: The code for the ESP-NOW LED receiver is located in the `main` branch.
- `led_controller.ino`: The code for the ESP-NOW LED controller is located in the `master` branch.

## Overview

This project enables wireless control of an LED matrix using ESP-NOW communication protocol between two ESP32 devices.

- `led_controller.ino`: This code runs on an ESP32 with a circular display (e.g., LilyGo T-RGB) and provides a graphical user interface (GUI) to control LED color, brightness, effects, and speed. It sends commands via ESP-NOW.
- `receiver.ino`: This code runs on an ESP32 connected to an 8x32 LED matrix (e.g., WS2812B). It receives commands via ESP-NOW from the controller and applies the specified color, brightness, and effects to the LED matrix using the FastLED library.

## Features

- **Wireless Control**: Utilizes ESP-NOW for low-latency communication.
- **Color Control**: Full RGB color selection.
- **Brightness Control**: Adjustable LED brightness.
- **Multiple Effects**: Solid, Rainbow, Fade, Strobe, and Pulse effects.
- **Speed Control**: Adjustable speed for dynamic effects.
- **Modular Design**: Separate codebases for controller and receiver for clear separation of concerns.

## Setup and Usage

### Hardware Requirements

- **Controller**: ESP32 development board with a circular display (e.g., LilyGo T-RGB 2.8 inches).
- **Receiver**: ESP32 development board.
- **LED Matrix**: 8x32 WS2812B LED matrix.

### Software Requirements

- Arduino IDE or PlatformIO.
- ESP32 Board Support Package for Arduino.
- **Libraries for `led_controller.ino` (master branch)**:
  - LilyGo_RGBPanel
  - LV_Helper
  - WiFi
  - esp_now
  - esp_wifi
- **Libraries for `receiver.ino` (main branch)**:
  - FastLED
  - WiFi
  - esp_now

### Configuration

1. **MAC Address**:
   - Upload `receiver.ino` to your receiver ESP32. Open the Serial Monitor and note down its MAC address.
   - Update the `broadcastAddress` array in `led_controller.ino` with the MAC address of your receiver.
2. **LED Pin**:
   - Verify the `LED_PIN` in `receiver.ino` matches the data pin you're using for your LED matrix.

### Uploading the Code

#### For the Receiver (`main` branch):

1. Clone or download the `main` branch of this repository.
2. Open `receiver.ino` in Arduino IDE.
3. Select your ESP32 board and port.
4. Install the required libraries (FastLED, etc.) via the Library Manager.
5. Upload the code to your receiver ESP32.

#### For the Controller (`master` branch):

1. Clone or download the `master` branch of this repository.
2. Open `led_controller.ino` in Arduino IDE.
3. Select your ESP32 board and port.
4. Install the required libraries (LilyGo_RGBPanel, LV_Helper, etc.) via the Library Manager.
5. Upload the code to your controller ESP32.

## Contributing

Feel free to open issues or submit pull requests.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
