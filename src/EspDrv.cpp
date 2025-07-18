#include "EspDrv.h"
#include <Arduino.h>
#include <avr/wdt.h>

#define DEBUG 1
#define ERROR 1
#if DEBUG
  #define PRINTLN_DEBUG(x) Serial.println(x)
  #define PRINT_DEBUG(x) Serial.print(x)
#else
  #define PRINTLN_DEBUG(x)
  #define PRINT_DEBUG(x)
#endif
#if ERROR
  #define PRINTLN_ERROR(x) Serial.println(x)
  #define PRINT_ERROR(x) Serial.print(x)
#else
  #define PRINTLN_ERROR(x)
  #define PRINT_ERROR(x)
#endif

EspDrv::EspDrv(Stream* serial) 
{
  this->serial = serial;
}

int EspDrv::CompareRingBuffer(const char* input)
{
  uint8_t length = strlen(input);
  uint8_t start = (ringBufferTail - length + ringBufferLength) % ringBufferLength;
  uint8_t i = 0;
  for(; i < length; i++)
  {
    if(ringBuffer[start] != input[i])
    {
      return 1;
    }
    start = (start + 1) % ringBufferLength;
  }
  return 0;
}

void EspDrv::ResetBuffer(uint8_t* buffer, uint16_t length)
{
  memset(buffer, 0, length);
}

void EspDrv::CheckTimeout()
{
  switch(this->state)
  {
    case EspReadState::STATUS:
      if(millis() - statusTimer > 1000)
      {
        this->state = EspReadState::IDLE;
        statusCounter = 0;
      }
    break;
    case EspReadState::DATA:
      if(millis() - startDataReadMillis > 5000)
      {
        this->state = EspReadState::IDLE;
        dataRead = 0;
        receivedDataLength = 0;
        ResetBuffer(receivedDataBuffer, receivedDataBufferSize);
      }
    break;
  }
}

void EspDrv::Loop() 
{
  CheckTimeout();
  while (this->serial->available()) 
  {
    CheckTimeout();
    int raw = this->serial->read();
    if(raw == -1)
    {
      continue;
    }
    char c = (char)raw;
    switch(this->state)
    {
      case EspReadState::STATUS:
        if(c >= '0' && c <= '5')
        {
          lastConnectionStatus = (int)(c - '0');
          this->state = EspReadState::IDLE;
          continue;
        }
        else
        {
          statusCounter++;
          if(statusCounter > 5 || millis() - statusTimer > 1000)
          {
            this->state = EspReadState::IDLE;
            statusCounter = 0;
            continue;
          }
        }
      break;
      case EspReadState::DATA_LENGTH:
        if (dataRead > 6)
        {
          if(this->lastState == EspReadState::STATUS)
          {
            statusTimer = millis();
            statusCounter = 0;
          }
          this->state = this->lastState;
          dataRead = 0;
          receivedDataLength = 0;
          return;
        }
        if (c == ':') 
        {
          receivedDataBuffer[dataRead++] = '\0';
          int result = sscanf(receivedDataBuffer, "%d", &receivedDataLength);
          if(result != 1 || receivedDataLength <= 0 || receivedDataLength > 512)
          {
            this->state = this->lastState;
            dataRead = 0;
            receivedDataLength = 0;
            ResetBuffer(receivedDataBuffer, receivedDataBufferSize);
            continue;
          }
          dataRead = 0;
          if(receivedDataBufferSize < receivedDataLength)
          {
            delete[] this->receivedDataBuffer;
            this->receivedDataBuffer = new uint8_t[receivedDataLength];
            this->receivedDataBufferSize = receivedDataLength;
          }
          this->state = EspReadState::DATA;
          startDataReadMillis = millis();
          ResetBuffer(receivedDataBuffer, receivedDataBufferSize);
          dataRead = 0;
        } 
        else 
        {
          if(c >= '0' && c <= '9')
          {
            this->receivedDataBuffer[dataRead++] = c;
          }
        }
      break;
      case EspReadState::DATA:
        receivedDataBuffer[dataRead++] = (uint8_t)raw;
        startDataReadMillis = millis();
        if (dataRead == receivedDataLength) 
        {
          this->state = EspReadState::IDLE;
          DataReceived(receivedDataBuffer, receivedDataLength);
          dataRead = 0;
          receivedDataLength = 0;
          ResetBuffer(receivedDataBuffer, receivedDataBufferSize);
          continue;
        }
      break;
    }
    if(c >= 32 && c <= 126)
    {
      ringBuffer[ringBufferTail] = c;
      ringBufferTail = (ringBufferTail + 1) % ringBufferLength;
    }
    if(this->state == EspReadState::IDLE && this->expectedTag != nullptr)
    {
      if (CompareRingBuffer(this->expectedTag) == 0) 
      {
        TagReceived(this->expectedTag);
        this->expectedTag = nullptr;
        this->tagMatchIndex = 0;
        return;
      } 
    }
    if (CompareRingBuffer("+IPD,") == 0) 
    {
      dataRead = 0;
      this->lastState = this->state;
      this->state = EspReadState::DATA_LENGTH;
      ringBufferTail = (ringBufferTail - 5 + ringBufferLength) % ringBufferLength;
    }
    else if (CompareRingBuffer("STATUS:") == 0 && this->state == EspReadState::IDLE && statusRequest) 
    {
      this->state = EspReadState::STATUS;
      statusTimer = millis();
      statusCounter = 0;
      statusRequest = false;
    }
    else if (CompareRingBuffer("CLOSED") == 0 && this->state == EspReadState::IDLE) 
    {
      lastConnectionStatus = GetConnectionStatus(true);
      return;
    }
  }
}

void EspDrv::Init(uint8_t receivedBufferSize)
{
  if(this->SendCmd(F("ATE0"), "OK", 1000))
  {
    if(this->SendCmd(F("AT+RST"), "OK", 30000))
    {
      delay(3000);
      if(this->SendCmd(F("ATE0"), "OK", 10000))
      {
        this->SendCmd(F("AT+CWMODE=1"), "OK", 1000);
      }
      lastConnectionStatus = GetConnectionStatus(true);
    }
  }
  this->receivedDataBuffer = new uint8_t[receivedBufferSize];
  this->receivedDataBufferSize = receivedBufferSize;
}

int EspDrv::Connect(const char* ssid, const char* password) 
{
  if(this->SendCmd(F("AT+CWJAP_CUR=\"%s\",\"%s\""), "OK", 10000, ssid, password))
  {
    delay(100);
    if(this->SendCmd(F("AT+CIPMUX=0"), "OK", 10000))
    {
      delay(100);
      lastConnectionStatus = GetConnectionStatus(true);
      return lastConnectionStatus == WL_CONNECTED;
    }
  }
  return WL_DISCONNECTED;
}

int EspDrv::TCPConnect(const char* url, int port) {
  if(this->SendCmd(F("AT+CIPSTART=\"TCP\",\"%s\",%d"), "OK", 10000, url, port))
  {
    delay(100);
    GetClientStatus(true);
    return 0;
  }
  return 0;
}

void EspDrv::Write(uint8_t* data, uint16_t length) 
{
  if(this->SendCmd(F("AT+CIPSEND=%d"), ">", 1000, length))
  {
    SendData(data, length);
    lastDataSend = millis();
  }
}

void EspDrv::SendData(uint8_t* data, uint16_t length) 
{
  this->serial->write(data, length);
  WaitForTag("SEND OK", 1000);
}

bool EspDrv::SendCmd(const __FlashStringHelper* cmd, const char* tag, unsigned long timeout, ...)
{
  if(this->state != EspReadState::IDLE)
  {
    return false;
  }
  char cmdBuf[CMD_BUFFER_SIZE];
  va_list args;
  va_start(args, cmd);
  vsnprintf_P(cmdBuf, CMD_BUFFER_SIZE, (char*)cmd, args);
  va_end(args);
  unsigned int t = millis();
  if(t - lastDataSend < 1000)
  {
    while(t - lastDataSend < 1000)
    {
      this->Loop();
      t = millis();
    }
  }
  Loop();
  PRINTLN_DEBUG(cmdBuf);
  this->serial->println(cmdBuf);
  bool tagResult = WaitForTag(tag, timeout);
  if(!tagResult)
  {
    PRINTLN_ERROR(cmdBuf);
  }
  this->expectedTag = nullptr;
  return tagResult;
}

bool EspDrv::WaitForTag(const char* pTag, unsigned long timeout) 
{
  this->expectedTag = pTag;
  unsigned long m = millis();
  unsigned long t = m;
  this->tag = "";
  while (strncmp(this->tag, pTag, strlen(pTag)) != 0 && t - m < timeout) 
  {
    wdt_reset();
    this->Loop();
    t = millis();
  }
  bool result = strncmp(this->tag, pTag, strlen(pTag)) == 0;
  if(!result)
  {
    PRINT_ERROR("Expected tag ");
    PRINT_ERROR(pTag);
    PRINT_ERROR(" received tag ");
    PRINTLN_ERROR(tag);
  }
  this->tag = "";
  return result;
}

void EspDrv::TagReceived(const char* pTag) 
{
  this->tag = pTag;
  this->expectedTag = nullptr;
}

void EspDrv::GetStatus(bool force)
{
  /*
  2 - GOT IP - může dojít k výpadku WiFi
  3 - TCP Connect - může dojít k výpadku tcp
  4 - TCP not conected
  5 - wifi not connected
  */
  if(millis() - statusRead < 1000 && !force && lastConnectionStatus != 5)
  {
    return;
  }
  statusRequest = true;
  this->SendCmd(F("AT+CIPSTATUS"), "OK", 1000);
  statusRequest = false;
  if(this->state == EspReadState::STATUS)
  {
    this->state = EspReadState::IDLE;
    statusRequest = false;
  }
  statusRead = millis();
}

int EspDrv::GetConnectionStatus()
{
  return GetConnectionStatus(false);
}
int EspDrv::GetConnectionStatus(bool force)
{
  GetStatus(force);
  if(lastConnectionStatus == 2 || lastConnectionStatus == 3 || lastConnectionStatus == 4)
  {
    return WL_CONNECTED;
  }
  else if(lastConnectionStatus == 5)
  {
		return WL_DISCONNECTED;
  }
	return WL_IDLE_STATUS;
}

uint8_t EspDrv::GetClientStatus()
{
  return GetClientStatus(false);
}
uint8_t EspDrv::GetClientStatus(bool force)
{
  GetStatus(force);
  if(lastConnectionStatus == 3)
  {
    return CL_CONNECTED;
  }
  return CL_DISCONNECTED;
}

void EspDrv::Disconnect()
{
  this->SendCmd(F("AT+CWQAP"), "OK", 1000);
}
