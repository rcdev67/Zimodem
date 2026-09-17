/* joypad.h - a Bluetooth game controller for the ST, see joypad.cpp */
void joypadSetup();
void joypadLoop();
const char *joypadStatus();   // model name of the connected controller, "NONE", or "UNSUPPORTED" in a build without Bluetooth
void joypadPair();            // drop the controller and its bond, accept a new one
