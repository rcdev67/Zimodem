/*
   A small status display on the modem: a 0.96 inch OLED, 128x64, four pins,
   SSD1306 or SH1106 on I2C. It shows which machine the modem serves, the
   WiFi, the address, the clock and what the modem is doing.

   Wiring on the ESP32-S3 DevKitC-1, all on its left header (the one a
   breadboard leaves reachable): GND, VCC to 3V3, SDA to GPIO8, SCL to
   GPIO7. GPIO4, 5, 6 and 9 there are Zimodem's modem lines. Without a
   display nothing changes: it is looked for every few seconds, and nothing
   waits for it.

   AT+SYS:<name>   the machine, sent by the companion when it finds the modem
   AT+DISP         what was found, "DISPLAY SSD1306 3C" or "DISPLAY NONE"
   AT+DISP:1306    the controller, stored: SSD1306 (most 0.96 inch ones)
   AT+DISP:1106    or SH1106 (the picture then sits two columns further left)
   AT+DISP:ON      AT+DISP:OFF, stored as well
*/
#ifndef ZDISPLAY_H
#define ZDISPLAY_H

#ifdef ZIMODEM_ESP32
void displaySetup();
void displayLoop();
void displayActivity(const char *fmt, ...);   // the last line, for a while
void displayProgress(int percent, const char *fmt, ...);   // a line and a bar below it
void displaySystem(const char *name);
void displayLoaded(const char *path);  // the last file fetched, kept on the last line
bool displayCommand(const char *arg, String &answer);
#else
static inline void displaySetup() {}
static inline void displayLoop() {}
static inline void displayActivity(const char *fmt, ...) {}
static inline void displayProgress(int percent, const char *fmt, ...) {}
static inline void displaySystem(const char *name) {}
static inline void displayLoaded(const char *path) {}
static inline bool displayCommand(const char *arg, String &answer) { return false; }
#endif

#endif
