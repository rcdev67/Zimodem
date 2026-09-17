// Bluepad32 alone on the C3 SuperMini: does the Xbox controller connect?
#include <Bluepad32.h>

static ControllerPtr ctl0 = nullptr;

void onConnected(ControllerPtr ctl) {
  ControllerProperties p = ctl->getProperties();
  Serial.printf("CONNECTED: %s vid %04x pid %04x\r\n", ctl->getModelName().c_str(), p.vendor_id, p.product_id);
  ctl0 = ctl;
}
void onDisconnected(ControllerPtr ctl) {
  Serial.printf("DISCONNECTED\r\n");
  if(ctl0 == ctl) ctl0 = nullptr;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.printf("bp32test: %s\r\n", BP32.firmwareVersion());
  const uint8_t* addr = BP32.localBdAddress();
  Serial.printf("bp32test: my address %02X:%02X:%02X:%02X:%02X:%02X\r\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
  BP32.setup(&onConnected, &onDisconnected);
  BP32.forgetBluetoothKeys();
  BP32.enableNewBluetoothConnections(true);
  Serial.printf("bp32test: scanning, put the controller into pairing mode\r\n");
}

void loop() {
  static unsigned long last = 0;
  BP32.update();
  if(millis() - last > 1000) {
    last = millis();
    if(ctl0 && ctl0->isConnected())
      Serial.printf("dpad %02x x %ld y %ld buttons %04x\r\n", ctl0->dpad(), (long)ctl0->axisX(), (long)ctl0->axisY(), ctl0->buttons());
    else
      Serial.printf(".\r\n");
  }
}
