
# ESP32 Live Football Scoreboard

A pocket-sized live football scoreboard built with an ESP32 and a 0.96" SSD1306 OLED display. It shows the two teams, the live score, and a running match clock — and flashes "GOAL!" with the scorer's name the moment a goal happens.

## Features

* Real-time match clock (MM:SS) that ticks smoothly, synced against live data
* Auto-detects any currently live match via the football-data.org API
* Flashes a "GOAL!" animation with scorer name and minute when the score changes
* Boot sequence: WiFi connecting -> WiFi connected -> waiting for a live match
* Includes a standalone demo mode (no WiFi/API needed) to preview the display behavior

## Hardware Required

* ESP32 Dev Module
* 0.96" SSD1306 OLED display (128x64, I2C)
* USB-OTG cable (if flashing from an Android phone)
* Jumper wires

## Wiring

| OLED Pin | ESP32 Pin |
|----------|-----------|
| VCC      | 3.3V      |
| GND      | GND       |
| SDA      | GPIO21    |
| SCL      | GPIO22    |

## Libraries Used

* ArduinoJson (Benoit Blanchon)
* Adafruit SSD1306
* Adafruit GFX Library
* WiFi.h / WiFiClientSecure.h / HTTPClient.h (built-in, ESP32 core)

## Setup

1. Get a free API key from football-data.org
2. Open the sketch and fill in your WiFi SSID, WiFi password, and API token
3. Select board: ESP32 Dev Module
4. Install the required libraries via Library Manager
5. Wire the OLED as described above
6. Upload and power on

## Demo Mode

A separate demo sketch is included that simulates a live match (Argentina vs England) without needing WiFi or an API key — useful for testing the display and GOAL animation on real hardware.

## Data Source

Live match data is pulled from football-data.org (free tier). Free tier covers 12 major competitions and does not require a paid subscription.

## License

MIT

## Author

Built by Thufail as part of an ongoing series of ESP32 + OLED hardware projects.
