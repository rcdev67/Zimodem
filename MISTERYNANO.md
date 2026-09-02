# Zimodem on an ESP32-C3 SuperMini as a WiFi modem for MiSTeryNano

This branch adds a configuration for the ESP32-C3 SuperMini so it can serve as
a Hayes compatible WiFi modem on the Atari ST's serial port of MiSTeryNano on
the Tang Nano 20K. The MiSTeryNano core routes the ST's RS232 port to the free
M0S connector when "Serial: Ext. UART" is selected in the OSD (branch
`nano20k-running` of https://github.com/rcdev67/MiSTeryNano).

## Wiring

| ESP32-C3 SuperMini | Tang Nano 20K header |
|---|---|
| pin 7 (GPIO7, modem TX) | 41 |
| pin 6 (GPIO6, modem RX) | 51 |
| GND | GND |
| 5V | 5V (or power the C3 from its own USB, not both) |

Leave pin 56 alone: pulling it low switches the companion's SPI link to an
external dock and the keyboard disappears.

## Flashing

One image, one command. Connect the C3 by USB (it shows up as an Espressif
USB serial port) and run:

    esptool --chip esp32c3 --port COMx --baud 460800 write-flash 0x0 zimodem-c3-supermini.bin

`esptool` comes with `pip install esptool`. Hold BOOT while plugging in if the
board does not enter download mode by itself.

## Using it from the ST

Set the terminal program to 19200 8N1 (the firmware's default). Then, like any
modem:

    AT                          -> OK
    ATW                         lists networks
    ATW"MyNetwork,MyPassword"   joins (one pair of quotes, comma inside; case matters)
    AT&W                        saves the settings, the modem reconnects on power up
    ATDT telehack.com:23        dials a telnet host; +++ returns to command mode

A plain Return ends a command, as on a real modem. Flash 1.6 works as is.

## Building

Arduino core esp32 3.x, board "Nologo ESP32C3 Super Mini", default partition
scheme (1.2 MB app / 1.5 MB SPIFFS). With arduino-cli:

    arduino-cli compile --fqbn esp32:esp32:nologo_esp32c3_super_mini:PartitionScheme=default zimodem

The configuration block is in `zimodem/zimodem.ino`
(`ARDUINO_NOLOGO_ESP32C3_SUPER_MINI`). Everything else is Bo Zimmerman's
Zimodem, unchanged, Apache 2.0 - see LICENSE and NOTICE.
