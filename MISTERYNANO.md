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

The modem fetches the resource into its own flash, answers `XMODEM <size>`
and sends it as XMODEM-CRC blocks, each acknowledged by the receiver. The
argument must be quoted: an unquoted argument ends at the first letter.

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
