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
| 5V | 5V |

Power the C3 from the Tang only. With its own USB cable plugged in at the same
time it back-powers the Tang through pin 41 and the SD card no longer starts.
Power the board off before plugging the C3 in or out.

Leave pin 56 alone: pulling it low switches the companion's SPI link to an
external dock and the keyboard disappears.

## Flashing

One image, one command. Connect the C3 by USB (it shows up as an Espressif
USB serial port) and run:

    esptool --chip esp32c3 --port COMx --baud 460800 write-flash 0x0 zimodem-c3-supermini.bin

`esptool` comes with `pip install esptool`. Hold BOOT while plugging in if the
board does not enter download mode by itself.

This full image also wipes the saved settings. To update a modem that is
already set up, flash only the program part (`zimodem.ino.bin` from a build)
at `0x10000`; the network settings in the SPIFFS partition stay.

## Using it from the ST

Set the terminal program to 19200 8N1 (the firmware's default). Then, like any
modem:

    at                          -> OK
    atw                         lists networks
    atw"MyNetwork,MyPassword"   joins (one pair of quotes, comma inside)
    at&w                        saves the settings, the modem reconnects on power up
    ati                         shows the connection and the IP address
    atdt telehack.com:23        dials a telnet host; +++ returns to command mode

Commands may be typed in lower case, so leave Caps Lock off: everything inside
the quotes is taken exactly as typed, and a password typed with Caps Lock on
is a different password. A plain Return ends a command, as on a real modem.
Flash 1.6 works as is.

After power up the modem needs a few seconds to join; `ati` answers
`ERROR ON MyNetwork` until it is in, then `CONNECTED TO MyNetwork (ip)`.

## Files for the companion: AT&G"xmodem:<url>"

The FPGA-Companion fork (branch `net-download`) uses the modem to load files
from a PC onto the SD card. It sends

    AT&G"xmodem:http://host:port/path"

The modem opens the resource, answers `XMODEM <size>` and sends it as
XMODEM-CRC blocks of 1 KB straight from the connection, each acknowledged
by the receiver. The companion runs the transfer at 460800 baud, set with
`ATB460800` before and `ATB19200` after. Nothing is staged in flash, so the size is only limited by the
card at the other end. The argument must be quoted: an unquoted argument
ends at the first letter.

## The same on an ESP32-S3 DevKitC-1

The stock `ARDUINO_ESP32S3_DEV` block works with two changes: the factory
reset pin is off (GPIO0 sits on the board's USB-UART auto-reset circuit
and read low for seconds while a PC was attached, which wiped the saved
WiFi settings every time), and WiFi sleep is off (with it pings took 250
ms and some were lost, web gets stalled). Wiring: GPIO16 (TX) to Tang 41,
GPIO15 (RX) to Tang 51, GND to GND. Power the board through its "UART"
USB socket, a jumper from the Tang's 5V pin let it brown out at every
WiFi join. Debug output is on that same socket at 115200. Flash with

    esptool --chip esp32s3 --port <UART port> --baud 460800 write_flash 0x0 zimodem.ino.merged.bin

built with

    arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=default_8MB zimodem

for the N16R8 module. Opening the UART port from a terminal resets the
board, so do not do that in the middle of a transfer.

## What is different from stock Zimodem on this board

- Transmit power 11 dBm: 15 dBm broke the join, 8.5 dBm lost 12% of the
  packets on this antenna. WiFi sleep is off, a lost link is retried after
  20 seconds (5 made it flap), a web get without a reply gives up after 15 s.
- The join at power up waits 20 seconds instead of 10.
- Debug output goes to the USB connector (115200 baud) instead of GPIO21, so
  a plain USB cable shows what the modem is doing. Note that Zimodem prints
  its whole configuration there at start, password included.

## Building

Arduino core esp32 3.x, board "Nologo ESP32C3 Super Mini", default partition
scheme (1.2 MB app / 1.5 MB SPIFFS). With arduino-cli:

    arduino-cli compile --fqbn esp32:esp32:nologo_esp32c3_super_mini:PartitionScheme=default zimodem

The configuration block is in `zimodem/zimodem.ino`
(`ARDUINO_NOLOGO_ESP32C3_SUPER_MINI`), the serial port choice in
`zimodem/pet2asc.h`. Everything else is Bo Zimmerman's Zimodem, unchanged,
Apache 2.0 - see LICENSE and NOTICE.
