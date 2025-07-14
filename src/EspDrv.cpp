#include "EspDrv.h"
#include <Arduino.h>
#include <avr/wdt.h>

#define DEBUG 0
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

const char* ESPTAGS[] = {
  "OK",
  "ERROR",
  "FAIL",
  "SEND OK",
  ">",
  "SEND FAIL",
  "STATUS"
};

EspDrv::EspDrv(Stream* serial) 
{
  this->serial = serial;
}

void EspDrv::Loop() 
{
  while (this->serial->available()) 
  {
    int raw = this->serial->read();
    if(raw == -1)
    {
      continue;
    }
    char c = (char)raw;
    switch(this->state)
    {
      case EspReadState::ESPREADSTATE_DATA_LENGTH:
        if (c == ':') 
        {
          buffer[bufLength++] = '\0';
          sscanf(buffer, "%d", &receivedDataLength);
          bufLength = 0;
          dataRead = 0;
          if(receivedDataBufferSize < receivedDataLength)
          {
            delete[] this->receivedDataBuffer;
            this->receivedDataBuffer = new uint8_t[receivedDataLength];
            this->receivedDataBufferSize = receivedDataLength;
          }
          this->state = EspReadState::ESPREADSTATE_DATA;
          startDataReadMillis = millis();
        } 
        else 
        {
          this->buffer[bufLength++] = c;
        }
      break;
      case EspReadState::ESPREADSTATE_DATA:
        receivedDataBuffer[dataRead++] = (uint8_t)raw;
        startDataReadMillis = millis();
        if (dataRead == receivedDataLength) 
        {
          this->state = EspReadState::ESPREADSTATE_IDLE;
          DataReceived(receivedDataBuffer, receivedDataLength);
        }
        if(millis() - startDataReadMillis > 5000)
        {
          this->state = EspReadState::ESPREADSTATE_IDLE;
        }
      break; 
      case EspReadState::ESPREADSTATE_LF:
        if (c == '\n') 
        {
          this->state = EspReadState::ESPREADSTATE_IDLE;
          this->bufLength = 0;
        }
      break;
      default: 
        if (c == '\r') 
        {
          this->state = EspReadState::ESPREADSTATE_LF;
          continue;
        }
        if(bufLength == 31)
        {
          bufLength = 0;
        }
        this->buffer[bufLength++] = c;
        this->buffer[bufLength]='\0';
        bool found = false;
        for (int i = 0; i < 7; i++) 
        {
          int compare = strncmp(ESPTAGS[i], this->buffer, strlen(ESPTAGS[i]));
          if (compare == 0) 
          {
            found = true;
            TagReceived(ESPTAGS[i]);
            bufLength = 0;
            break;
          }
        }
        if(found)
        {
          break;
        }
        if (strncmp("+IPD,", this->buffer, strlen("+IPD,")) == 0) 
        {
          this->state = EspReadState::ESPREADSTATE_DATA_LENGTH;
          bufLength = 0;
        }
        else if (strncmp("CLOSED", this->buffer, strlen("CLOSED")) == 0) 
        {
          lastConnectionStatus = GetConnectionStatus(true);
          bufLength = 0;
        }
        else if(writeToStatusBuffer)
        {
          this->statusBuffer[statusBufferLength++] = c;
        }
      break;
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
    this->data = data;
    this->dataLength = length;
    SendData();
    lastDataSend = millis();
  }
}

void EspDrv::SendData() 
{
  this->serial->write(this->data, this->dataLength);
  WaitForTag("SEND OK", 1000);
}

bool EspDrv::SendCmd(const __FlashStringHelper* cmd, const char* tag, unsigned long timeout, ...)
{
  if(this->state != EspReadState::ESPREADSTATE_IDLE)
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
  bool needsToReadToStatusBuffer = writeToStatusBuffer;
  writeToStatusBuffer = false;
  Loop();
  writeToStatusBuffer = needsToReadToStatusBuffer;
  PRINTLN_DEBUG(cmdBuf);
  this->serial->println(cmdBuf);
  bool tagResult = WaitForTag(tag, timeout);
  if(!tagResult)
  {
    PRINTLN_ERROR(cmdBuf);
  }
  return tagResult;
}

bool EspDrv::WaitForTag(const char* pTag, unsigned long timeout) 
{
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
  if(this->SendCmd(F("AT+CIPSTATUS"), "STATUS", 1000))
  {
    statusBufferLength = 0;
    writeToStatusBuffer = true;
    if(WaitForTag("OK", 1000))
    {
      writeToStatusBuffer = false;
      statusBuffer[statusBufferLength] = '\0';
      PRINT_DEBUG("Status buffer ");
      PRINTLN_DEBUG(statusBuffer);
      sscanf(statusBuffer, ":%d", &lastConnectionStatus);
      statusRead = millis();
    }
  }
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
