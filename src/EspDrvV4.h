#ifndef __ESPDRVV4_H
#define __ESPDRVV4_H

#define CMD_BUFFER_SIZE 200
#define PER_BYTE_BUDGET_MS 50UL

#include <Arduino.h>
#include "IEspDrv.h"


enum class EspV4ReadState {
  IDLE = 0,          // čeká na data/odpovědi
  DATA_LENGTH,       // čtení délky dat za +IPD
  DATA,              // čtení samotných dat +IPD
  STATUS,
  BUSY,
  CWJAP              // čtení odpovědi +CWJAP: (RSSI)
};

class EspDrvV4 : public IEspDrv
{
  private:
    Stream *serial;
    unsigned char ringBuffer[16] = {};
    uint8_t ringBufferLength = 16;
    uint8_t ringBufferTail = 0;
    EspV4ReadState state = EspV4ReadState::IDLE;
    EspV4ReadState lastState = EspV4ReadState::IDLE;
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
    int lastConnectionStatus = 0;
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
    int8_t lastRssi = 0;
    uint8_t cwjapCommaCount = 0;
    int16_t cwjapRssiAcc = 0;
    bool cwjapRssiNeg = false;
    bool cwjapInQuotes = false;
    bool cwjapFound = false;
    bool secure = false;
    uint8_t sslAuthMode = 2;
    const char* sntpServer = nullptr;
    int8_t sntpTimezone = 0;

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
    EspDrvV4(Stream *serial);
    // interByteTimeoutMs: timeout mezi dvěma bajty, chytá mrtvou linku.
    // fixedTimeoutReserveMs: fixní rezerva přičtená k cumulative timeoutu.
    // Cumulative timeout = receivedDataLength * PER_BYTE_BUDGET_MS + fixedTimeoutReserveMs.
    // PER_BYTE_BUDGET_MS = 50 ms je konzervativní pro UART rychlosti od 9600 Bd výš
    // s rezervou na ESP processing overhead.
    void Init(uint8_t receivedBufferSize,
              uint16_t maxAllowedDataLength = 512,
              unsigned long interByteTimeoutMs = 1000,
              unsigned long fixedTimeoutReserveMs = 1000);
    // Musí být zavoláno před TCPConnect. authMode odpovídá AT+CIPSSLCCONF:
    // 0 = bez ověření serveru, 2 = ověření proti CA v client_ca.0.
    // SNI se nastavuje automaticky z hostname předaného do TCPConnect.
    void SetSecure(bool enabled, uint8_t authMode = 2);
    // Povinné pro authMode 2 - ověření platnosti certifikátu potřebuje reálný čas.
    // Bez toho ESP startuje v roce 1970 a každý TCPConnect selže.
    // Server zadávejte jako IP adresu; výchozí NTP jména se bez DNS nepřeloží.
    // Aplikuje se v Connect() po připojení k WiFi.
    void SetTimeSource(const char* server, int8_t timezone = 0);
    // Alternativa k SetTimeSource pro zařízení s RTC.
    // Volat až po úspěšném Connect().
    bool SetTime(unsigned long unixTime);
    int Connect(const char* ssid, const char* password);
    int TCPConnect(const char* url, int port) override;
    void Disconnect();
    bool Write(uint8_t* data, uint16_t length) override;
    void Loop() override;
    int GetConnectionStatus();
    uint8_t GetClientStatus() override;
    void Close() override;
    void Reset();
    uint8_t GetMemAllocFailCount();
    uint8_t GetTagRecognitionFailCount();
    int8_t GetRssi();

    // Callbacky níže a zděděný DataReceived jsou volány synchronně z Loop().
    // NESMÍ volat metody EspDrvV4, které vedou na SendCmd
    // (Close, Reset, Disconnect, GetConnectionStatus(true),
    //  GetClientStatus(true)). Pro takové akce nastavte vlastní flag
    // a metodu zavolejte z hlavního loop() aplikace.
    void (*DataTimeout)() = nullptr;
    void (*DataIgnored)(uint16_t length) = nullptr;
    void (*OnBusy)(uint8_t count) = nullptr;
};
#endif
