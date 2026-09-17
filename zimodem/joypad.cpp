/*
   A Bluetooth game controller for the Atari ST, next to the modem.

   Built with the Bluepad32 board package (esp32-bluepad32) the modem
   also pairs a Bluetooth controller and reports it as one byte on a
   second UART, 115200 8N1, to the MiSTeryNano core (pin 54 on the Tang
   Nano 20K). Bit order matches the companion's HID joysticks: 0 right,
   1 left, 2 down, 3 up, 4 fire (A), 5 second button (B). The byte goes
   out on every change and every 200 ms as a keep-alive; the core drops
   the joystick when nothing has arrived for half a second.

   With the plain esp32 core this file compiles to nothing.
*/

#include <Arduino.h>
#include "joypad.h"

#if __has_include(<Bluepad32.h>)
#include <Bluepad32.h>
#define debugPrintf Serial.printf

#if defined(ARDUINO_NOLOGO_ESP32C3_SUPER_MINI) || defined(ARDUINO_MAKERGO_C3_SUPERMINI)
#  define JOYPAD_UART_NUM 0      /* the modem sits on UART 1 (pins 6/7) */
#  define JOYPAD_PIN_TX   21     /* the pin marked TX on the SuperMini */
#  define JOYPAD_PIN_RX   20
#elif defined(ARDUINO_ESP32S3_DEV)
#  define JOYPAD_UART_NUM 2      /* UART 0 is the debug console, 1 the modem */
#  define JOYPAD_PIN_TX   17
#  define JOYPAD_PIN_RX   18
#else
#  define JOYPAD_UART_NUM 2
#  define JOYPAD_PIN_TX   17
#  define JOYPAD_PIN_RX   16
#endif

static HardwareSerial joySerial(JOYPAD_UART_NUM);
static ControllerPtr  joyCtl = nullptr;
static uint8_t        joyLast = 0xff;
static unsigned long  joySent = 0;

void joypadConnected(ControllerPtr ctl)
{
  ControllerProperties p = ctl->getProperties();
  debugPrintf("Joypad: %s connected, vid %04x pid %04x\r\n", ctl->getModelName().c_str(), p.vendor_id, p.product_id);
  if(joyCtl == nullptr)
    joyCtl = ctl;
}

void joypadDisconnected(ControllerPtr ctl)
{
  debugPrintf("Joypad: disconnected\r\n");
  if(joyCtl == ctl)
  {
    joyCtl = nullptr;
    joySerial.write((uint8_t)0);     // release at once, not after the watchdog
    joyLast = 0;
  }
}

void joypadSetup()
{
  joySerial.begin(115200, SERIAL_8N1, JOYPAD_PIN_RX, JOYPAD_PIN_TX);
  BP32.setup(&joypadConnected, &joypadDisconnected);
  BP32.enableNewBluetoothConnections(true);
  debugPrintf("Joypad: Bluepad32 %s ready, joystick bytes on GPIO%d\r\n", BP32.firmwareVersion(), JOYPAD_PIN_TX);
}

uint8_t joypadState()
{
  if((joyCtl == nullptr) || !joyCtl->isConnected() || !joyCtl->isGamepad())
    return 0;
  uint8_t b = 0;
  uint8_t d = joyCtl->dpad();
  if(d & DPAD_RIGHT) b |= 0x01;
  if(d & DPAD_LEFT)  b |= 0x02;
  if(d & DPAD_DOWN)  b |= 0x04;
  if(d & DPAD_UP)    b |= 0x08;
  // the left stick as well, a good third of its travel
  if(joyCtl->axisX() >  180) b |= 0x01;
  if(joyCtl->axisX() < -180) b |= 0x02;
  if(joyCtl->axisY() >  180) b |= 0x04;
  if(joyCtl->axisY() < -180) b |= 0x08;
  if(joyCtl->a()) b |= 0x10;
  if(joyCtl->b()) b |= 0x20;
  return b;
}

const char *joypadStatus()
{
  static char name[40];
  if((joyCtl == nullptr) || !joyCtl->isConnected())
    return "NONE";
  snprintf(name, sizeof(name), "%s", joyCtl->getModelName().c_str());
  return name;
}

void joypadPair()
{
  if((joyCtl != nullptr) && joyCtl->isConnected())
    joyCtl->disconnect();
  BP32.forgetBluetoothKeys();
  BP32.enableNewBluetoothConnections(true);
  debugPrintf("Joypad: bonds forgotten, waiting for a controller in pairing mode\r\n");
}

void joypadLoop()
{
  BP32.update();
  uint8_t now = joypadState();
  unsigned long t = millis();
  if((now != joyLast) || ((t - joySent) > 200))
  {
    joySerial.write(now);
    joyLast = now;
    joySent = t;
  }
}

#else
void joypadSetup() {}
void joypadLoop() {}
const char *joypadStatus() { return "UNSUPPORTED"; }
void joypadPair() {}
#endif
