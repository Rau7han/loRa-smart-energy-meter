This project demonstrates a LoRa-based smart prepaid energy meter built using ESP32. The system measures real-time electrical parameters using the PZEM-004T energy meter and transmits the data wirelessly through a LoRa (SX1278) module. The readings are shown locally on a TFT display and remotely through a web dashboard.

The project includes two nodes: a transmitter node for measurement and control, and a receiver node for monitoring and remote access.

Features

Real-time monitoring of voltage, current, power, frequency, and energy
LoRa-based long-range wireless communication
Local TFT display for live readings
Remote monitoring using web dashboard
Relay control from dashboard
Prepaid-style monitoring concept (wallet-based usage idea)
Compact design with 3D printed enclosure

System Overview

The transmitter node measures electrical parameters using the PZEM-004T module. The ESP32 processes the data and displays it on a TFT screen. The readings are transmitted via LoRa to the receiver node.

The receiver node receives LoRa data and sends it to a web dashboard over WiFi. The dashboard allows real-time monitoring and remote relay control.

Hardware Components
Transmitter Node

ESP32 Development Board
PZEM-004T Energy Meter Module
SX1278 LoRa (RA-02) Module
1.8" TFT Display (ST7735)
Relay Module (5V)
Push Button
HLK AC-DC Power Module
Perfboard + Connectors + Wiring

Receiver Node

ESP32 (ESP32-S3)
SX1278 LoRa Module
OLED Display (0.96")
WiFi Dashboard

Folder Structure
lora-smart-energy-meter
│
├── Transmitter
│   └── transmitter_code.ino
│
├── Receiver
│   └── receiver_code.ino
│
├── diagrams
│   ├── wiring_diagram.png
│   └── block_diagram.png
│
├── Images
│   ├── full_setup.jpg
│   ├── transmitter.jpg
│   └── receiver.jpg
│
└── README.md
Transmitter Pinout (ESP32)

LoRa (SPI)
SCK → GPIO18
MISO → GPIO19
MOSI → GPIO23
NSS → GPIO5
RST → GPIO14
DIO0 → GPIO26

TFT Display
CS → GPIO5
DC → GPIO21
RST → GPIO22
MOSI → GPIO23
SCK → GPIO18

PZEM-004T
TX → RX2 (GPIO16)
RX → TX2 (GPIO17)

Relay → GPIO2
Push Button → GPIO4

Receiver Pinout (ESP32-S3)

LoRa (SPI)
SCK → GPIO12
MISO → GPIO11
MOSI → GPIO10
NSS → GPIO15
RST → GPIO14
DIO0 → GPIO13

OLED (I2C)
SDA → GPIO17
SCL → GPIO18

How It Works

The transmitter reads electrical values from PZEM-004T and updates the TFT display.
It sends the data continuously via LoRa.
The receiver captures LoRa packets and pushes them to a web dashboard.
Users can monitor usage and control the relay remotely.

This project demonstrates a LoRa-based smart prepaid energy meter built using ESP32. The system measures real-time electrical parameters using the PZEM-004T energy meter and transmits the data wirelessly through a LoRa (SX1278) module. The readings are shown locally on a TFT display and remotely through a web dashboard.

The project includes two nodes: a transmitter node for measurement and control, and a receiver node for monitoring and remote access.

Features

Real-time monitoring of voltage, current, power, frequency, and energy
LoRa-based long-range wireless communication
Local TFT display for live readings
Remote monitoring using web dashboard
Relay control from dashboard
Prepaid-style monitoring concept (wallet-based usage idea)
Compact design with 3D printed enclosure

System Overview

The transmitter node measures electrical parameters using the PZEM-004T module. The ESP32 processes the data and displays it on a TFT screen. The readings are transmitted via LoRa to the receiver node.

The receiver node receives LoRa data and sends it to a web dashboard over WiFi. The dashboard allows real-time monitoring and remote relay control.

Hardware Components
Transmitter Node

ESP32 Development Board
PZEM-004T Energy Meter Module
SX1278 LoRa (RA-02) Module
1.8" TFT Display (ST7735)
Relay Module (5V)
Push Button
HLK AC-DC Power Module
Perfboard + Connectors + Wiring

Receiver Node

ESP32 (ESP32-S3)
SX1278 LoRa Module
OLED Display (0.96")
WiFi Dashboard

Folder Structure
lora-smart-energy-meter
│
├── Transmitter
│   └── transmitter_code.ino
│
├── Receiver
│   └── receiver_code.ino
│
├── diagrams
│   ├── wiring_diagram.png
│   └── block_diagram.png
│
├── Images
│   ├── full_setup.jpg
│   ├── transmitter.jpg
│   └── receiver.jpg
│
└── README.md
Transmitter Pinout (ESP32)

LoRa (SPI)
SCK → GPIO18
MISO → GPIO19
MOSI → GPIO23
NSS → GPIO5
RST → GPIO14
DIO0 → GPIO26

TFT Display
CS → GPIO5
DC → GPIO21
RST → GPIO22
MOSI → GPIO23
SCK → GPIO18

PZEM-004T
TX → RX2 (GPIO16)
RX → TX2 (GPIO17)

Relay → GPIO2
Push Button → GPIO4

Receiver Pinout (ESP32-S3)

LoRa (SPI)
SCK → GPIO12
MISO → GPIO11
MOSI → GPIO10
NSS → GPIO15
RST → GPIO14
DIO0 → GPIO13

OLED (I2C)
SDA → GPIO17
SCL → GPIO18

How It Works

The transmitter reads electrical values from PZEM-004T and updates the TFT display.
It sends the data continuously via LoRa.

The receiver captures LoRa packets and pushes them to a web dashboard.
Users can monitor usage and control the relay remotely.
