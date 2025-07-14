#ifndef __ESPDRV_H
#define __ESPDRV_H

#define CMD_BUFFER_SIZE 200

#define ESP_NOTCONNECTED 0
#define ESP_CONNECTED 1

#define WL_DISCONNECTED 0
#define WL_CONNECTED 1
#define WL_IDLE_STATUS -1

#define CL_DISCONNECTED 0
#define CL_CONNECTED 1

#include <Arduino.h>


enum EspReadState {
  ESPREADSTATE_IDLE = 0,          // čeká na data/odpovědi
  ESPREADSTATE_DATA_LENGTH,       // čtení délky dat za +IPD
  ESPREADSTATE_DATA,              // čtení samotných dat +IPD
  ESPREADSTATE_LF                 // čeká na '\n' po '\r'
};

class EspDrv
{
  private:
    Stream *serial;
    char buffer[32];
    char statusBuffer[57];
    int bufLength = 0;
    int statusBufferLength = 0;
    bool writeToStatusBuffer = false;
    EspReadState state = EspReadState::ESPREADSTATE_IDLE;
    uint8_t *data;
    uint16_t dataLength = 0;
    uint8_t* receivedDataBuffer;
    uint16_t receivedDataBufferSize = 0;
    uint16_t receivedDataLength;
    uint16_t dataRead = 0;
    const char* tag = "";
    void SendData();
    bool EspDrv::SendCmd(const __FlashStringHelper* cmd, const char* tag, unsigned long timeout, ...);
    void TagReceived(const char* pTag);
    bool WaitForTag(const char* pTag, unsigned long timeout);
    void GetStatus(bool force);
    int GetConnectionStatus(bool force);
    uint8_t GetClientStatus(bool force);
    uint8_t connectionState = ESP_NOTCONNECTED;
    unsigned long startDataReadMillis = 0;
    unsigned long statusRead = 0;
    int lastConnectionStatus = 5;
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
    int GetConnectionStatus();
    uint8_t GetClientStatus();
};
#endif