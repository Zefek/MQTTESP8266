#ifndef __ESPDRV_H
#define __ESPDRV_H

#define CMD_BUFFER_SIZE 200
#define PER_BYTE_BUDGET_MS 50UL

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
    unsigned char ringBuffer[16] = {};
    uint8_t ringBufferLength = 16;
    uint8_t ringBufferTail = 0;
    EspReadState state = EspReadState::IDLE;
    EspReadState lastState = EspReadState::IDLE;
    uint8_t* receivedDataBuffer = nullptr;
    uint16_t receivedDataBufferSize = 0;
    uint16_t receivedDataLength = 0;
    uint16_t dataRead = 0;
    uint16_t maxAllowedDataLength = 512;
    bool ignoreReceivedData = false;
    bool closeRequested = false;
    bool inClose = false;
    unsigned long interByteTimeoutMs = 1000;
    unsigned long fixedTimeoutReserveMs = 1000;
    unsigned long dataStartedMillis = 0;
    const char* tag = "";
    unsigned long startDataReadMillis = 0;
    unsigned long statusRead = 0;
    int lastConnectionStatus = 5;
    unsigned long lastDataSend = 0;
    unsigned long statusTimer = 0;
    uint8_t statusCounter = 0;
    const char* expectedTag = nullptr;
    bool statusFound = false;
    unsigned long busyTimeout = 0;
    unsigned long busyTime = 0;
    uint8_t busyTryCount = 0;
    uint8_t memAllocFailCount = 0;
    uint8_t tagRecognitionFailCount = 0;

    bool SendData(uint8_t* data, uint16_t length);
    bool SendCmd(const __FlashStringHelper* cmd, const char* tag, unsigned long timeout, ...);
    void TagReceived(const char* pTag);
    bool WaitForTag(const char* pTag, unsigned long timeout);
    void GetStatus(bool force);
    int GetConnectionStatus(bool force);
    uint8_t GetClientStatus(bool force);
    int CompareRingBuffer(const char* input);
    void ResetBuffer(uint8_t* buffer, uint16_t length);
    void CheckTimeout();
    void WaitUntilReady();

  public:
    EspDrv(Stream *serial);
    // interByteTimeoutMs: timeout mezi dvěma bajty, chytá mrtvou linku.
    // fixedTimeoutReserveMs: fixní rezerva přičtená k cumulative timeoutu.
    // Cumulative timeout = receivedDataLength * PER_BYTE_BUDGET_MS + fixedTimeoutReserveMs.
    // PER_BYTE_BUDGET_MS = 50 ms je konzervativní pro UART rychlosti od 9600 Bd výš
    // s rezervou na ESP processing overhead.
    void Init(uint8_t receivedBufferSize,
              uint16_t maxAllowedDataLength = 512,
              unsigned long interByteTimeoutMs = 1000,
              unsigned long fixedTimeoutReserveMs = 1000);
    int Connect(const char* ssid, const char* password);
    int TCPConnect(const char* url, int port);
    void Disconnect();
    bool Write(uint8_t* data, uint16_t length);
    void Loop();
    int GetConnectionStatus();
    uint8_t GetClientStatus();
    void Close();
    void Reset();
    uint8_t GetMemAllocFailCount();
    uint8_t GetTagRecognitionFailCount();

    // Callbacky níže jsou volány synchronně z Loop().
    // NESMÍ volat metody EspDrv, které vedou na SendCmd
    // (Close, Reset, Disconnect, GetConnectionStatus(true),
    //  GetClientStatus(true)). Pro takové akce nastavte vlastní flag
    // a metodu zavolejte z hlavního loop() aplikace.
    void (*DataReceived) (uint8_t* buffer, int length) = nullptr;
    void (*DataTimeout)() = nullptr;
    void (*DataIgnored)(uint16_t length) = nullptr;
    void (*OnBusy)(uint8_t count) = nullptr;
};
#endif