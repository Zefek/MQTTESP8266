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
  IDLE = 0,          // čeká na data/odpovědi
  DATA_LENGTH,       // čtení délky dat za +IPD
  DATA,              // čtení samotných dat +IPD
  STATUS,
  BUSY
};

class EspDrv
{
  private:
    Stream *serial;
    unsigned char ringBuffer[16];
    uint8_t ringBufferLength = 16;
    uint8_t ringBufferTail = 0;
    EspReadState state = EspReadState::IDLE;
    EspReadState lastState = EspReadState::IDLE;
    uint8_t* receivedDataBuffer;
    uint16_t receivedDataBufferSize = 0;
    uint16_t receivedDataLength;
    uint16_t dataRead = 0;
    const char* tag = "";
    unsigned long startDataReadMillis = 0;
    unsigned long statusRead = 0;
    int lastConnectionStatus = 5;
    unsigned long lastDataSend = 0;
    unsigned long statusTimer = 0;
    uint8_t statusCounter = 0;
    const char* expectedTag = nullptr;
    uint8_t tagMatchIndex = 0;
    bool statusRequest = false;
    unsigned long busyTimeout = 0;
    unsigned long busyTime = 0;
    uint8_t busyTryCount = 0;
    uint8_t memAllocFailCount = 0;
    uint8_t tagRecognitionFailCount = 0;

    void SendData(uint8_t* data, uint16_t length);
    bool SendCmd(const __FlashStringHelper* cmd, const char* tag, unsigned long timeout, ...);
    void TagReceived(const char* pTag);
    bool WaitForTag(const char* pTag, unsigned long timeout);
    void GetStatus(bool force);
    int GetConnectionStatus(bool force);
    uint8_t GetClientStatus(bool force);
    int CompareRingBuffer(const char* input);
    void ResetBuffer(uint8_t* buffer, uint16_t length);
    void CheckTimeout();

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
    void Close();
    void Reset();
    uint8_t GetMemAllocFailCount();
    uint8_t GetTagRecognitionFailCount();
};
#endif