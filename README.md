USB Pendrive to ESP8266 — Feasibility & Options

Summary
-------
The ESP8266 does not have USB host capability. You cannot plug a USB flash drive directly into a standard ESP8266 module and expect it to work. Below are practical alternatives and wiring/code notes.

Options
-------
1) Use a USB host controller module (CH376S or MAX3421E-based USB Host Shield) connected to the ESP8266.
   - CH376S: inexpensive serial/SPI-based USB mass storage interface. Use SPI mode and a library to read FAT files. Connect VCC/GND, SPI pins (SCK/MOSI/MISO/CS) and an IRQ if available.
   - USB Host Shield (MAX3421E): designed for Arduino; requires 5V tolerant SPI and library support. More complex and larger.

2) Use a microcontroller with native USB host (recommended): ESP32-S2/S3 or Raspberry Pi Pico (with host support) — these can act as USB hosts and interface to flash drives more easily.

3) Use an intermediary device (PC/Router) to share files over network (HTTP/FTP) to the ESP8266. Plug USB into PC and serve files via HTTP; ESP downloads over Wi‑Fi.

Suggested approach for ESP8266 + CH376S (SPI)
--------------------------------------------
- Wire: VCC=3.3V, GND=GND, SCK -> GPIO14, MISO -> GPIO12, MOSI -> GPIO13, CS -> GPIO15 (example). Ensure voltage levels match.
- Use an SPI-based CH376S driver/library and FAT reader. Read files and then serve them via the ESP8266 webserver or write to SPIFFS/LittleFS if needed.

Notes and cautions
------------------
- Power: USB drives can draw >100mA; ensure the module supplies sufficient 5V power through the USB host controller. Many tiny ESP8266 boards cannot supply USB power directly.
- Level shifting: CH376S modules may be 5V; use proper level shifting for signals if needed.
- Pin conflicts: On many sketches the I2C pins (GPIO4/5) are used by the OLED — avoid assigning the DHT or other sensors to the same pin.

Minimal workflow idea
---------------------
1. Connect CH376S to ESP8266 via SPI and power the USB device via CH376S's VBUS.
2. On ESP, initialize SPI and CH376S, mount FAT, read file list and file contents.
3. Serve file contents via your web UI endpoints (example: `/download?file=...`).

References
----------
Search for "CH376S ESP8266" and "MAX3421E USB Host Shield ESP8266" for example code and wiring diagrams.

Prepared on: 2026-06-23
