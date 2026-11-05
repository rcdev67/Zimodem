/*
   The status display, see zdisplay.h.

   Two halves. displayLoop() runs in the modem's own loop: once a second it
   puts together what is to be shown -- WiFi, address, clock, mode -- as
   plain text, because only there may the modem's state be read. A task of
   its own does all of the I2C: it looks for the display, draws the text
   and sends it. Sending a whole picture takes some 25 ms; in that task the
   serial port of the modem never waits for it.

   A picture that never changes burns into an OLED over the years, so the
   whole picture moves one column every minute.
*/
#ifdef ZIMODEM_ESP32
#include <Wire.h>
#include <Preferences.h>
#include "zdisplay_font.h"

#define DISPLAY_SDA     8
#define DISPLAY_SCL     7
#define DISPLAY_I2C_HZ  400000
#define DISPLAY_LINES   6                 // text lines below the name
#define DISPLAY_COLS    21                // 6 pixels each
#define DISPLAY_NAME    11                // the name, twice the size

struct DisplayText
{
  char sys[DISPLAY_NAME + 1];
  char line[DISPLAY_LINES][DISPLAY_COLS + 1];
  int bars;                               // the WiFi, 0..4, -1 none
  int progress;                           // 0..100 with a bar on the last line, -1 none
};

static SemaphoreHandle_t dispLock = NULL;
static DisplayText dispNext;              // written by displayLoop()
static volatile bool dispDirty = false;
static char dispSys[DISPLAY_NAME + 1] = "MISTrcdev";
static char dispAct[DISPLAY_COLS + 1] = "";
static unsigned long dispActUntil = 0;
static int dispProg = -1;                 // the bar under dispAct, -1 none
static char dispLast[DISPLAY_COLS + 1] = "";   // the last file fetched, "> name"
static char dispMsgTitle[DISPLAY_NAME + 1] = "";  // a message over the whole screen
static char dispMsg[DISPLAY_LINES][DISPLAY_COLS + 1];
static unsigned long dispMsgUntil = 0;
static unsigned long dispNextMs = 0;
static volatile bool dispSh1106 = false;
static volatile bool dispOn = true;
static volatile bool dispReinit = false;
static volatile uint8_t dispAddr = 0;     // 0: none found (yet)
static uint8_t dispFb[8][128];            // 8 pages of 8 rows, bit 0 on top
static DisplayText dispShown;             // what the task draws

// ---------------------------------------------------------- the I2C task --

static bool dispCmds(const uint8_t *c, int n)
{
  Wire.beginTransmission(dispAddr);
  Wire.write((uint8_t)0x00);              // commands follow
  for(int i=0;i<n;i++)
    Wire.write(c[i]);
  return Wire.endTransmission() == 0;
}

static bool dispProbe(uint8_t a)
{
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

static bool dispInit()
{
  // both in page addressing, the mode the SH1106 only knows
  static const uint8_t ssd1306[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x02,
    0xA1, 0xC8, 0xDA, 0x12, 0x81, 0x8F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
    0x2E };
  static const uint8_t sh1106[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0xAD, 0x8B, 0xA1, 0xC8,
    0xDA, 0x12, 0x81, 0x8F, 0xD9, 0x22, 0xDB, 0x35, 0x32, 0xA4, 0xA6 };
  return dispSh1106 ? dispCmds(sh1106, sizeof(sh1106)) : dispCmds(ssd1306, sizeof(ssd1306));
}

static bool dispPush()
{
  uint8_t col = dispSh1106 ? 2 : 0;       // the SH1106 has 132 columns, the glass shows 2..129
  for(int p=0;p<8;p++)
  {
    uint8_t c[3] = { (uint8_t)(0xB0 + p), (uint8_t)(col & 0x0f), (uint8_t)(0x10 | (col >> 4)) };
    if(!dispCmds(c, 3))
      return false;
    for(int x=0;x<128;x+=32)              // the Wire buffer holds 128 bytes
    {
      Wire.beginTransmission(dispAddr);
      Wire.write((uint8_t)0x40);          // data follow
      Wire.write(&dispFb[p][x], 32);
      if(Wire.endTransmission() != 0)
        return false;
    }
  }
  return true;
}

static void dispChar(int x, int page, char ch, bool big)
{
  if((ch < 32) || (ch > 126))
    ch = '?';
  const uint8_t *g = displayFont[ch - 32];
  for(int i=0;i<5;i++)
  {
    if(!big)
    {
      if((x + i >= 0) && (x + i < 128))
        dispFb[page][x + i] = g[i];
      continue;
    }
    uint16_t w = 0;                       // the column twice as high ...
    for(int b=0;b<7;b++)
      if(g[i] & (1 << b))
        w |= 3 << (2 * b);
    for(int k=0;k<2;k++)                  // ... and twice as wide
    {
      int xx = x + 2 * i + k;
      if((xx >= 0) && (xx < 128))
      {
        dispFb[page][xx] = w & 0xff;
        dispFb[page + 1][xx] = w >> 8;
      }
    }
  }
}

static void dispText(int x, int page, const char *s, bool big)
{
  for(;*s && (x < 128);s++, x += big ? 12 : 6)
    dispChar(x, page, *s, big);
}

// draws dispShown; a global, as the Arduino builder puts prototypes
// above the struct otherwise
static void dispRender(int shift)
{
  const DisplayText &t = dispShown;
  memset(dispFb, 0, sizeof(dispFb));
  int w = strlen(t.sys) * 12 - 2;
  dispText((128 - w) / 2 + shift, 0, t.sys, true);
  for(int i=0;i<DISPLAY_LINES;i++)
    dispText(shift, 2 + i, t.line[i], false);
  if(t.progress >= 0)                     // a bar on the last line, 4 to 123 filled
  {
    int fill = 4 + (120 * t.progress) / 100;
    for(int x=1;x<127;x++)
    {
      int xx = x + shift - 1;
      if((xx < 0) || (xx >= 128))
        continue;
      if((x == 1) || (x == 126))
        dispFb[7][xx] = 0x7e;                 // the ends
      else
        dispFb[7][xx] = (x >= 3 && x <= fill) ? 0x7e : 0x42;   // filled, or top and bottom
    }
  }
  if(t.bars >= 0)                         // four bars at the end of the WiFi line
  {
    for(int b=0;b<4;b++)
    {
      int h = 2 * (b + 1);
      uint8_t m = (b < t.bars) ? (uint8_t)(0xff << (8 - h)) : 0x80;
      for(int k=0;k<3;k++)
      {
        int x = 108 + b * 5 + k + shift - 1;
        if((x >= 0) && (x < 128))
          dispFb[2][x] = m;
      }
    }
  }
}

static void displayTask(void *arg)
{
  Wire.begin(DISPLAY_SDA, DISPLAY_SCL, DISPLAY_I2C_HZ);
  bool ready = false;
  bool lit = false;
  int shownShift = -1;
  // On the common 5 V rail the display gets its power in the same moment as
  // the S3. Asked right away, it acknowledged its address but missed the
  // commands, the charge pump above all, and stayed dark; after a reset of
  // the S3 alone, as after flashing, it came up. So a second's patience
  // first, and every ten seconds the pump and the display on again: harmless
  // when it runs, and it brings back one that missed the start or a dip.
  unsigned long nextProbe = millis() + 1000;
  unsigned long nextWake = 0;
  for(;;)
  {
    if(ready && dispOn && (millis() >= nextWake))
    {
      static const uint8_t wake1306[] = { 0x8D, 0x14, 0xAF };
      static const uint8_t wake1106[] = { 0xAD, 0x8B, 0xAF };
      nextWake = millis() + 10000;
      bool ok = dispSh1106 ? dispCmds(wake1106, sizeof(wake1106)) : dispCmds(wake1306, sizeof(wake1306));
      if(!ok)
      {
        ready = false;
        dispAddr = 0;
        nextProbe = millis() + 5000;
      }
      else
        lit = true;
    }
    if((!ready || dispReinit) && (millis() >= nextProbe))
    {
      dispReinit = false;
      dispAddr = dispProbe(0x3C) ? 0x3C : (dispProbe(0x3D) ? 0x3D : 0);
      ready = (dispAddr != 0) && dispInit();
      lit = false;
      if(ready)
      {
        dispDirty = true;
        nextWake = millis() + 2000;       // the first time soon after the init
      }
      else
      {
        dispAddr = 0;
        nextProbe = millis() + 5000;      // plugged in later? looked for again
      }
    }
    int shift = (millis() / 60000) & 1;
    if(ready && (dispDirty || (shift != shownShift)))
    {
      dispDirty = false;
      xSemaphoreTake(dispLock, portMAX_DELAY);
      dispShown = dispNext;
      xSemaphoreGive(dispLock);
      dispRender(shift);
      shownShift = shift;
      bool ok = dispPush();
      if(ok && (lit != dispOn))
      {
        uint8_t c = dispOn ? 0xAF : 0xAE;
        ok = dispCmds(&c, 1);
        lit = dispOn;
      }
      if(!ok)
      {
        ready = false;
        dispAddr = 0;
        nextProbe = millis() + 5000;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ------------------------------------------------- the modem's own side --

static void dispSet(char *dst, size_t size, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(dst, size, fmt, ap);
  va_end(ap);
}

static void displayCompose()
{
  DisplayText t;
  memset(&t, 0, sizeof(t));
  strncpy(t.sys, dispSys, DISPLAY_NAME);
  t.bars = -1;
  t.progress = -1;

  // a message from the companion takes the whole screen, while the
  // machine's own screen is dark
  if(dispMsgTitle[0] && (millis() < dispMsgUntil))
  {
    strncpy(t.sys, dispMsgTitle, DISPLAY_NAME);
    for(int i=0;i<DISPLAY_LINES;i++)
      strncpy(t.line[i], dispMsg[i], DISPLAY_COLS);
    xSemaphoreTake(dispLock, portMAX_DELAY);
    if(memcmp(&t, &dispNext, sizeof(t)) != 0)
    {
      dispNext = t;
      dispDirty = true;
    }
    xSemaphoreGive(dispLock);
    return;
  }

  if(WiFi.status() == WL_CONNECTED)
  {
    int rssi = WiFi.RSSI();
    t.bars = (rssi >= -55) ? 4 : (rssi >= -65) ? 3 : (rssi >= -72) ? 2 : (rssi >= -80) ? 1 : 0;
    dispSet(t.line[0], sizeof(t.line[0]), "WiFi %d dBm", rssi);
    dispSet(t.line[1], sizeof(t.line[1]), "%s", WiFi.localIP().toString().c_str());
    dispSet(t.line[2], sizeof(t.line[2]), "%s", WiFi.SSID().c_str());
  }
  else
  if(wifiSSI.length() > 0)
  {
    dispSet(t.line[0], sizeof(t.line[0]), "WiFi joining");
    dispSet(t.line[2], sizeof(t.line[2]), "%s", wifiSSI.c_str());
  }
  else
    dispSet(t.line[0], sizeof(t.line[0]), "WiFi not set up");

  if(zclock.isTimeSet())
  {
    DateTimeClock &c = zclock.getCurrentTime();
    dispSet(t.line[3], sizeof(t.line[3]), "%04d-%02d-%02d %02d:%02d",
            c.getYear(), c.getMonth(), c.getDay(), c.getHour(), c.getMinute());
  }
  else
    dispSet(t.line[3], sizeof(t.line[3]), "clock not set");

  const char *mode = "ready";
  if(currMode == &streamMode)
    mode = "online";
#if INCLUDE_PPP
  else
  if(currMode == &pppMode)
    mode = "PPP";
#endif
#if INCLUDE_SLIP
  else
  if(currMode == &slipMode)
    mode = "SLIP";
#endif
  dispSet(t.line[4], sizeof(t.line[4]), "modem %s, %d", mode, baudRate);

  if(millis() < dispActUntil)
  {
    if(dispProg >= 0)                     // the transfer above its bar
    {
      strncpy(t.line[4], dispAct, DISPLAY_COLS);
      t.line[5][0] = 0;
      t.progress = dispProg;
    }
    else
      strncpy(t.line[5], dispAct, DISPLAY_COLS);
  }
  else
  if(dispLast[0])                         // what came last, until the next one
    strncpy(t.line[5], dispLast, DISPLAY_COLS);

  xSemaphoreTake(dispLock, portMAX_DELAY);
  if(memcmp(&t, &dispNext, sizeof(t)) != 0)
  {
    dispNext = t;
    dispDirty = true;
  }
  xSemaphoreGive(dispLock);
}

void displaySetup()
{
  Preferences p;
  p.begin("zdisp", true);
  dispSh1106 = p.getBool("sh1106", false);
  dispOn = p.getBool("on", true);
  p.end();
  dispLock = xSemaphoreCreateMutex();
  memset(&dispNext, 0, sizeof(dispNext));
  displayCompose();
  // low priority, on the core WiFi runs on: the modem's loop stays alone on its own
  xTaskCreatePinnedToCore(displayTask, "display", 4096, NULL, 1, NULL, 0);
}

void displayLoop()
{
  if(dispLock == NULL)
    return;
  if(millis() < dispNextMs)
    return;
  dispNextMs = millis() + 1000;
  displayCompose();
}

void displayActivity(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(dispAct, sizeof(dispAct), fmt, ap);
  va_end(ap);
  dispActUntil = millis() + 15000;
  dispProg = -1;
  dispNextMs = 0;                         // shown with the next displayLoop()
}

void displayProgress(int percent, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(dispAct, sizeof(dispAct), fmt, ap);
  va_end(ap);
  dispProg = (percent < 0) ? 0 : (percent > 100) ? 100 : percent;
  dispActUntil = millis() + 15000;
  dispNextMs = 0;
}

void displaySystem(const char *name)
{
  // the companion sends its core's name; Zimodem has lowered it
  static const char *known[][2] = {
    { "misterynano", "Atari ST" }, { "st", "Atari ST" },
    { "c64nano", "C64" },          { "c64", "C64" },
    { "nanomig", "Amiga" },        { "amiga", "Amiga" } };
  for(size_t i=0;i<sizeof(known)/sizeof(known[0]);i++)
    if(strcmp(name, known[i][0]) == 0)
    {
      strncpy(dispSys, known[i][1], DISPLAY_NAME);
      dispSys[DISPLAY_NAME] = 0;
      dispNextMs = 0;
      return;
    }
  strncpy(dispSys, name, DISPLAY_NAME);
  dispSys[DISPLAY_NAME] = 0;
  dispNextMs = 0;
}

// "Title|text|more text": the title large, the text wrapped over the six
// lines below, a new line at every further '|'. '_' stands for a space, so
// the message gets through a command line in one piece. Stays five minutes
// or until the next one, "30:" in front makes it 30 seconds; an empty one
// clears it.
void displayMessage(const char *msg)
{
  memset(dispMsg, 0, sizeof(dispMsg));
  dispMsgTitle[0] = 0;
  dispNextMs = 0;
  if((msg == NULL) || (*msg == 0))
    return;
  // "30:Title|text": shown 30 seconds instead of five minutes
  unsigned long secs = 300;
  const char *semi = strchr(msg, ':');
  if(semi && (semi > msg) && (semi - msg <= 4) && isdigit((unsigned char)*msg))
  {
    secs = strtoul(msg, NULL, 10);
    msg = semi + 1;
  }
  size_t n = 0;
  for(;*msg && (*msg != '|');msg++)
    if(n < DISPLAY_NAME)
      dispMsgTitle[n++] = (*msg == '_') ? ' ' : *msg;
  dispMsgTitle[n] = 0;
  int line = 0;
  size_t col = 0;
  if(*msg == '|')
    msg++;
  for(;*msg && (line < DISPLAY_LINES);msg++)
  {
    if(*msg == '|')
    {
      line++;
      col = 0;
      continue;
    }
    if(col == DISPLAY_COLS)
    {
      line++;
      col = 0;
      if(line == DISPLAY_LINES)
        break;
    }
    dispMsg[line][col++] = (*msg == '_') ? ' ' : *msg;
  }
  dispMsgUntil = millis() + secs * 1000;
}

// The name of a file fetched, from the path of its URL: the part after the
// last '/', without a query, with %XX decoded. A path that ends in '/' is a
// folder list and changes nothing.
void displayLoaded(const char *path)
{
  if(path == NULL)
    return;
  const char *s = strrchr(path, '/');
  s = s ? s + 1 : path;
  char name[DISPLAY_COLS];
  size_t n = 0;
  for(;*s && (*s != '?') && (n < sizeof(name) - 1);s++)
  {
    char c = *s;
    if((c == '%') && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2]))
    {
      char hex[3] = { s[1], s[2], 0 };
      c = (char)strtol(hex, NULL, 16);
      s += 2;
    }
    else
    if(c == '+')
      c = ' ';
    name[n++] = c;
  }
  name[n] = 0;
  if(n == 0)
    return;
  snprintf(dispLast, sizeof(dispLast), "> %s", name);
  dispNextMs = 0;
}

// arg is what follows AT+DISP, lowered: "", ":1306", ":1106", ":on", ":off"
bool displayCommand(const char *arg, String &answer)
{
  if(*arg == ':')
  {
    arg++;
    Preferences p;
    p.begin("zdisp", false);
    if(strcmp(arg, "1306") == 0)      { dispSh1106 = false; p.putBool("sh1106", false); dispReinit = true; }
    else if(strcmp(arg, "1106") == 0) { dispSh1106 = true;  p.putBool("sh1106", true);  dispReinit = true; }
    else if(strcmp(arg, "on") == 0)   { dispOn = true;  p.putBool("on", true);  dispDirty = true; }
    else if(strcmp(arg, "off") == 0)  { dispOn = false; p.putBool("on", false); dispDirty = true; }
    else { p.end(); return false; }
    p.end();
  }
  else
  if(*arg != 0)
    return false;
  char buf[48];
  if(dispAddr)
    snprintf(buf, sizeof(buf), "DISPLAY %s %02X%s", dispSh1106 ? "SH1106" : "SSD1306",
             dispAddr, dispOn ? "" : " OFF");
  else
    snprintf(buf, sizeof(buf), "DISPLAY NONE");
  answer = buf;
  return true;
}
#endif
