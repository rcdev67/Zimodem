/*
   Copyright 2018-2026 Bo Zimmerman

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include <FS.h>

typedef int (*RecvChar)(ZSerial *ser, int);
typedef void (*SendChar)(ZSerial *ser, char);
typedef bool (*DataHandler)(File *xfile, unsigned long, char*, int);

class XModem 
{
  typedef enum
  {
    Crc,
    ChkSum  
  } transfer_t;
  
  private:
    size_t fileSizeLimit = 10000000;
    //holds readed byte (due to dataAvail())
    int byte;
    //expected block number
    unsigned char blockNo;
    //extended block number, send to dataHandler()
    unsigned long blockNoExt;
    //retry counter for NACK
    int retries;
    //buffer
    char buffer[1024];
    //repeated block flag
    bool repeatedBlock;

    ZSerial xserial;

    int  (*recvChar)(ZSerial *ser, int);
    void (*sendChar)(ZSerial *ser, char);
    bool (*dataHandler)(File *xfile, unsigned long number, char *buffer, int len);
    unsigned short crc16_ccitt(char *buf, int size);
    bool serialAvail(int delay);
    int serialRead(int delay);
    void serialWrite(char symbol);
    bool receiveFrameNo(void);
    bool receiveData(int blkSize);
    bool checkCrc(int blkSize);
    bool checkChkSum(void);
    bool receiveFrames(transfer_t transfer);
    bool sendNack(void);
    void init(void);
    
    bool transmitFrame(transfer_t transfer, int blkSize);
    bool transmitFrames(transfer_t);
    unsigned char generateChkSum(void);

  protected:
    FS *fileSystem = null;
    File yfile;
    File *xfile = null;
    File *dfile = null;
    int blockSize = 128;
    bool send0block = false;
    
  public:
    void setBlockSize(int sz) { blockSize = sz; }   // 128 (SOH) or 1024 (STX)
    static const unsigned char XMO_NACK = 21;
    static const unsigned char XMO_ACK =  6;
    static const unsigned char XMO_CRC = 'C';

    static const unsigned char XMO_SOH =  1;
    static const unsigned char XMO_STX =  2;
    static const unsigned char XMO_EOT =  4;
    static const unsigned char XMO_CAN =  0x18;

    static const int receiveFrameDelay=7000;
    static const int receiveByteDelay=3000;
    static const int rcvRetryLimit = 10;

    size_t fileSize = 0;

    XModem(File &f, FlowControlType commandFlow, RecvChar recvChar, SendChar sendChar, DataHandler dataHandler);
    bool receive();
    bool transmit();
};

class YModem : public XModem
{
  public:
    YModem(FS *fileSystem, File &f, FlowControlType commandFlow, RecvChar recvChar, SendChar sendChar, DataHandler dataHandler);
};

static int xReceiveSerial(ZSerial *ser, int del)
{
  unsigned long end=micros() + (del * 1000L);
  while(micros() < end)
  {
    serialOutDeque();
    if(ser->available() > 0)
    {
      int c=ser->read();
      return c;
    }
    yield();
  }
  return -1;
}

static void xSendSerial(ZSerial *ser, char c)
{
  ser->write((uint8_t)c);
  ser->flush();
}

static bool xUDataHandler(File *xfile, unsigned long number, char *buf, int sz)
{
  for(int i=0;i<sz;i++)
    xfile->write((uint8_t)buf[i]);
  return true;
}

static bool xDDataHandler(File *xfile, unsigned long number, char *buf, int sz)
{
  for(int i=0;i<sz;i++)
  {
    int c=xfile->read();
    if(c<0)
    {
      if(i==0)
        return false;
      buf[i] = (char)26;
    }
    else
      buf[i] = (char)c;
  }
  return true;  
}

static bool xDownload(FlowControlType commandFlow, File &f, String &errors)
{
  XModem xmo(f, commandFlow, xReceiveSerial, xSendSerial, xDDataHandler);
  bool result = xmo.transmit();
  return result;
}

/* Send a web resource as XMODEM straight from its socket. Staging it in
   SPIFFS first limited the size to the partition and, on the S3, took
   longer than the receiver was willing to wait. The socket is read one
   block at a time; the block stays in the XModem buffer for resends. */
static WiFiClient *xStreamClient = NULL;
static uint32_t    xStreamLeft   = 0;

static bool xSDataHandler(File *xfile, unsigned long number, char *buf, int sz)
{
  if((xStreamClient == NULL) || (xStreamLeft == 0))
    return false;
  int i = 0;
  unsigned long last = millis();
  while((i < sz) && (xStreamLeft > 0))
  {
    if(xStreamClient->available() > 0)
    {
      int c = xStreamClient->read();
      if(c < 0)
        continue;
      buf[i++] = (char)c;
      xStreamLeft--;
      last = millis();
    }
    else
    {
      if(!xStreamClient->connected())
        break;                              // the source went away
      if((millis() - last) > 15000)
        break;                              // the source stalled
      yield();
    }
  }
  if(i == 0)
    return false;
  while(i < sz)
    buf[i++] = (char)26;
  return true;
}

/* The resource in PSRAM first, then XMODEM from there. Sent straight
   from the socket, the web server was tied to the serial line: at the
   C64's 38400 baud the ESP32 takes 3.8 KB/s, its TCP window stays shut,
   and the server's probes for it back off to seconds. A 330 KB disk
   stalled block after block until the 15 s limit above ended it short;
   at the ST's 460800 it hardly ever showed. Over WiFi the whole file is
   in in under a second. */
#ifdef ZIMODEM_ESP32
static const uint8_t *xMemData = NULL;
static uint32_t       xMemLeft = 0;

static bool xMDataHandler(File *xfile, unsigned long number, char *buf, int sz)
{
  if(xMemLeft == 0)
    return false;
  int n = (xMemLeft < (uint32_t)sz) ? (int)xMemLeft : sz;
  memcpy(buf, xMemData, n);
  xMemData += n;
  xMemLeft -= n;
  while(n < sz)
    buf[n++] = (char)26;
  return true;
}

// NULL when there is no PSRAM or not enough of it: then nothing has been
// read and the stream can still be sent as it is. *failed is set when the
// reading itself broke off; then the resource is lost for this request.
static uint8_t *xFetchToMemory(WiFiClient *c, uint32_t length, bool *failed)
{
  *failed = false;
  if(!psramFound())
    return NULL;
  uint8_t *mem = (uint8_t *)ps_malloc(length);
  if(mem == NULL)
    return NULL;
  uint32_t got = 0;
  unsigned long last = millis();
  while(got < length)
  {
    int a = c->available();
    if(a > 0)
    {
      uint32_t want = length - got;
      int r = c->read(mem + got, ((uint32_t)a < want) ? a : want);
      if(r > 0)
      {
        got += r;
        last = millis();
      }
    }
    else
    {
      if(!c->connected())
        break;
      if((millis() - last) > 15000)
        break;
      delay(1);
    }
  }
  if(got != length)
  {
    free(mem);
    *failed = true;
    return NULL;
  }
  return mem;
}

static bool xDownloadMemory(FlowControlType commandFlow, uint8_t *mem, uint32_t length, String &errors)
{
  File dummy;
  xMemData = mem;
  xMemLeft = length;
  XModem xmo(dummy, commandFlow, xReceiveSerial, xSendSerial, xMDataHandler);
  xmo.setBlockSize(1024);
  bool result = xmo.transmit();
  bool all = (xMemLeft == 0);
  xMemData = NULL;
  xMemLeft = 0;
  return result && all;
}
#endif

static bool xDownloadStream(FlowControlType commandFlow, WiFiClient *c, uint32_t length, String &errors)
{
  File dummy;
  xStreamClient = c;
  xStreamLeft   = length;
  XModem xmo(dummy, commandFlow, xReceiveSerial, xSendSerial, xSDataHandler);
  xmo.setBlockSize(1024);   // one acknowledge per KB instead of per 128 bytes
  bool result = xmo.transmit();
  xStreamClient = NULL;
  return result && (xStreamLeft == 0);
}

static bool xUpload(FlowControlType commandFlow, File &f, String &errors)
{
  XModem xmo(f, commandFlow, xReceiveSerial, xSendSerial, xUDataHandler);
  bool result = xmo.receive();
  return result;
}

static bool yDownload(FS *fileSystem, FlowControlType commandFlow, File &f, String &errors)
{
  YModem ymo(fileSystem, f, commandFlow, xReceiveSerial, xSendSerial, xDDataHandler);
  ymo.fileSize = f.size();  // multi-upload will need this
  bool result = ymo.transmit();
  return result;
}

static bool yUpload(FS *fileSystem, FlowControlType commandFlow, File &dirF, String &errors)
{
  YModem ymo(fileSystem, dirF, commandFlow, xReceiveSerial, xSendSerial, xUDataHandler);
  bool result = ymo.receive();
  return result;
}
