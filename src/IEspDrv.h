#ifndef __IESPDRV_H
#define __IESPDRV_H

#include <Arduino.h>

#define WL_DISCONNECTED 0
#define WL_CONNECTED 1
#define WL_IDLE_STATUS -1

#define CL_DISCONNECTED 0
#define CL_CONNECTED 1

class IEspDrv
{
  public:
    virtual bool Write(uint8_t* data, uint16_t length) = 0;
    virtual void Loop() = 0;
    virtual uint8_t GetClientStatus() = 0;
    virtual int TCPConnect(const char* url, int port) = 0;
    virtual void Close() = 0;

    void (*DataReceived) (uint8_t* buffer, int length) = nullptr;

  protected:
    ~IEspDrv() = default;
};
#endif
