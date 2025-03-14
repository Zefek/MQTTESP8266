#ifndef __ESPDRV_H
#define __ESPDRV_H

#define ESPREADSTATE1  1
#define ESPREADSTATE2  2
#define ESPREADSTATE_DATA_LENGTH1 3
#define ESPREADSTATE_DATA1 4
#define ESPREADSTATE_DATA2 5
#define CMD_BUFFER_SIZE 200

#define ESP_NOTCONNECTED 0
#define ESP_CONNECTED 1

#define WL_DISCONNECTED 0
#define WL_CONNECTED 1
#define WL_IDLE_STATUS -1

#define CL_DISCONNECTED 0
#define CL_CONNECTED 1

#include <Arduino.h>

class EspDrv
{
  private:
    Stream *serial;
    char buffer[32];
    char statusBuffer[57];
    int bufLength = 0;
    int statusBufferLength = 0;
    bool writeToStatusBuffer = false;
    uint8_t state = ESPREADSTATE1;
    uint8_t *data;
    uint16_t dataLength = 0;
    uint8_t* receivedDataBuffer;
    uint16_t receivedDataLength;
    uint16_t dataRead = 0;
    const char* tag = "";
    void SendData();
    void SendCmd(const __FlashStringHelper* cmd, ...);
    void TagReceived(const char* pTag);
    bool WaitForTag(const char* pTag, unsigned long timeout);
    void GetStatus();
    uint8_t connectionState = ESP_NOTCONNECTED;
    unsigned long startDataReadMillis = 0;
    unsigned long statusRead = 0;
    int lastConnectionStatus = 0;
    unsigned long lastDataSend = 0;

  public:
    EspDrv(Stream *serial);
    void Init(uint8_t receivedBufferSize);
    int Connect(const char* ssid, const char* password);
    int TCPConnect(const char* url, int port);
    void Disconnect();
    void Write(uint8_t* data, uint16_t length);
    void Loop();
    void (*DataReceived) (uint8_t* buffer, int length);
    uint8_t GetConnectionStatus();
    uint8_t GetClientStatus();
};
#endif