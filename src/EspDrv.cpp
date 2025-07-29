#include "EspDrv.h"
#include <Arduino.h>
#include <avr/wdt.h>
#include <ctype.h>

#define TRACE 0
#define DEBUG 0
#define WARNING 1
#define ERROR 1
#if TRACE
  #define PRINTLN_TRACE(x) do { Serial.print(F("TRACE")); Serial.println(x); } while(0);
  #define PRINT_TRACE(x) do { Serial.print(F("TRACE")); Serial.print(x); } while(0)
#else
  #define PRINTLN_TRACE(x)
  #define PRINT_TRACE(x)
#endif
#if DEBUG
  #define PRINTLN_DEBUG(x) do { Serial.print(F("DEBUG:"));  Serial.println(x); } while(0)
  #define PRINT_DEBUG(x) do { Serial.print(F("DEBUG:")); Serial.print(x); } while(0)
#else
  #define PRINTLN_DEBUG(x) 
  #define PRINT_DEBUG(x)
#endif
#if WARNING
  #define PRINTLN_WARNING(x) do { Serial.print(F("WARN:")); Serial.println(x); } while(0)
  #define PRINT_WARNING(x) do { Serial.print(F("WARN:")); Serial.print(x); } while(0)
#else
  #define PRINTLN_WARNING(x)
  #define PRINT_WARNING(x)
#endif
#if ERROR
  #define PRINTLN_ERROR(x) do{ Serial.print(F("ERROR:")); Serial.println(x); } while(0)
  #define PRINT_ERROR(x) do { Serial.print(F("ERROR:")); Serial.print(x); } while(0)
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
    if(tolower((unsigned char)ringBuffer[start]) != tolower((unsigned char)input[i]))
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
        PRINTLN_WARNING(F("Status timout expired."));
        this->state = EspReadState::IDLE;
        statusCounter = 0;
      }
    break;
    case EspReadState::DATA:
    case EspReadState::DATA_LENGTH:
      if(millis() - startDataReadMillis > 3000)
      {
        PRINTLN_WARNING(F("Data timout expired."));
        this->state = EspReadState::IDLE;
        dataRead = 0;
        receivedDataLength = 0;
        ResetBuffer(receivedDataBuffer, receivedDataBufferSize);
      }
    break;
    case EspReadState::BUSY:
      if(millis() - busyTime > busyTimeout)
      {
        PRINTLN_WARNING(F("Busy timout expired."));
        this->state = EspReadState::IDLE;
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
    #if TRACE
    if((raw >= 32 && raw <= 126) || raw == 13 || raw == 10)
    {
      Serial.print((char)raw);
    }
    else
    {
      Serial.print("|");
      Serial.print(raw);
      Serial.print("|");
    }
    #endif
    char c = (char)raw;
    switch(this->state)
    {
      case EspReadState::STATUS:
        if(c >= '0' && c <= '5')
        {
          lastConnectionStatus = (int)(c - '0');
          PRINT_DEBUG("Connection status ");
          PRINTLN_DEBUG(lastConnectionStatus);
          this->state = EspReadState::IDLE;
          continue;
        }
        else
        {
          statusCounter++;
          if(statusCounter > 5 || millis() - statusTimer > 1000)
          {
            statusCounter = 0;
            this->state = EspReadState::IDLE;
            continue;
          }
        }
      break;
      case EspReadState::DATA_LENGTH:
        startDataReadMillis = millis();
        if (dataRead > 6)
        {
          startDataReadMillis = millis();
          if(this->lastState == EspReadState::STATUS)
          {
            statusTimer = millis();
            statusCounter = 0;
          }
          dataRead = 0;
          receivedDataLength = 0;
          this->state = this->lastState;
          return;
        }
        if (c == ':') 
        {
          receivedDataBuffer[dataRead++] = '\0';
          int result = sscanf(receivedDataBuffer, "%d", &receivedDataLength);
          PRINT_DEBUG("Data length ");
          PRINTLN_DEBUG(receivedDataLength);
          if(result != 1 || receivedDataLength <= 0 || receivedDataLength > 512)
          {
            dataRead = 0;
            receivedDataLength = 0;
            ResetBuffer(receivedDataBuffer, receivedDataBufferSize);
            this->state = this->lastState;
            continue;
          }
          dataRead = 0;
          if(receivedDataBufferSize < receivedDataLength)
          {
            PRINT_DEBUG(F("Need data buffer allocation. Current size: "));
            PRINT_DEBUG(receivedDataBufferSize);
            PRINT_DEBUG(F(". New size: "));
            PRINTLN_DEBUG(receivedDataLength);
            uint8_t* newBuffer = new uint8_t[receivedDataLength];
            if(!newBuffer)
            {
              PRINT_ERROR(F("Error in data buffer allocation. Expected size: "));
              PRINT_ERROR(receivedDataLength);
              PRINT_ERROR(F(". Current size: "));
              PRINTLN_ERROR(receivedDataBufferSize);
              memAllocFailCount = memAllocFailCount == 255? memAllocFailCount : memAllocFailCount + 1;              
              ResetBuffer(receivedDataBuffer, receivedDataBufferSize);
              dataRead = 0;
              this->state = EspReadState::IDLE;
              continue;
            }
            else
            {
              delete[] this->receivedDataBuffer;
              this->receivedDataBuffer = newBuffer;
              this->receivedDataBufferSize = receivedDataLength;
              memAllocFailCount = 0;
            }
          }
          startDataReadMillis = millis();
          ResetBuffer(receivedDataBuffer, receivedDataBufferSize);
          dataRead = 0;
          this->state = EspReadState::DATA;
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
          PRINTLN_DEBUG(F("Read all received data."));
          DataReceived(receivedDataBuffer, receivedDataLength);
          dataRead = 0;
          receivedDataLength = 0;
          ResetBuffer(receivedDataBuffer, receivedDataBufferSize);
          statusRead = millis();
          this->state = busyTryCount > 0? EspReadState::BUSY : EspReadState::IDLE;
          continue;
        }
        PRINT_TRACE(F("Read "));
        PRINT_TRACE(dataRead);
        PRINT_TRACE(F("/"));
        PRINTLN_TRACE(receivedDataLength);
      break;
    }
    if(c >= 32 && c <= 126)
    {
      ringBuffer[ringBufferTail] = c;
      ringBufferTail = (ringBufferTail + 1) % ringBufferLength;
    }
    if((this->state == EspReadState::IDLE || this->state == EspReadState::BUSY) && this->expectedTag != nullptr)
    {
      if (CompareRingBuffer(this->expectedTag) == 0) 
      {
        PRINT_DEBUG(F("Tag recognized "));
        PRINTLN_DEBUG(this->expectedTag);
        TagReceived(this->expectedTag);
        this->expectedTag = nullptr;
        if(this->state == EspReadState::BUSY)
        {
          busyTimeout = 0;
          busyTryCount = 0;
          this->state = EspReadState::IDLE;
        }
        return;
      } 
    }
    if (CompareRingBuffer("+IPD,") == 0) 
    {
      PRINTLN_DEBUG(F("+IPD"));
      ringBufferTail = (ringBufferTail - 5 + ringBufferLength) % ringBufferLength;
      dataRead = 0;
      startDataReadMillis = millis();
      this->lastState = this->state;
      this->state = EspReadState::DATA_LENGTH;
    }
    else if (CompareRingBuffer("STATUS:") == 0 && (this->state == EspReadState::IDLE || this->state == EspReadState::BUSY) && !statusFound) 
    {
      PRINTLN_DEBUG(F("STATUS"));
      statusTimer = millis();
      statusCounter = 0;
      statusFound = true;
      busyTimeout = 0;
      busyTryCount = 0;
      this->state = EspReadState::STATUS;
    }
    else if (CompareRingBuffer("CLOSED") == 0 && (this->state == EspReadState::IDLE || this->state == EspReadState::BUSY)) 
    {
      PRINTLN_DEBUG(F("CLOSED"));
      if(this->state == EspReadState::BUSY)
      {
        busyTimeout = 0;
        busyTryCount = 0;
        this->state = EspReadState::IDLE;
      }
      GetConnectionStatus(true);
    }
    else if (CompareRingBuffer("BUSY") == 0 && this->state == EspReadState::IDLE)
    {
      PRINTLN_WARNING(F("BUSY"));
      if(busyTryCount > 10)
      {
        busyTimeout = 0;
        busyTryCount = 0;
        this->state = EspReadState::IDLE;
        Close();
        continue;
      }
      else
      {
        busyTryCount++;
        busyTimeout = min(busyTimeout * 2 + random(200, 1000), 5000);
        busyTime = millis();
        this->state = EspReadState::BUSY;
      }
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
      GetConnectionStatus(true);
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
      int status = GetConnectionStatus(true);
      return status == WL_CONNECTED;
    }
  }
  return WL_DISCONNECTED;
}

int EspDrv::TCPConnect(const char* url, int port)
{
  if(this->SendCmd(F("AT+CIPSTART=\"TCP\",\"%s\",%d"), "OK", 10000, url, port))
  {
    delay(100);
    GetClientStatus(true);
    return 0;
  }
  return 0;
}

bool EspDrv::Write(uint8_t* data, uint16_t length) 
{
  bool result = false;
  if(this->SendCmd(F("AT+CIPSEND=%d"), ">", 1000, length))
  {
    result = SendData(data, length);
    lastDataSend = millis();
  }
  if(result)
  {
    statusRead = millis();
  }
  return result;
}

void EspDrv::WaitUntilReady()
{
  do
  {
    Loop();
  } while(this->state != EspReadState::IDLE || millis() - lastDataSend < 1000);
}

bool EspDrv::SendData(uint8_t* data, uint16_t length) 
{
  WaitUntilReady();
  this->serial->write(data, length);
  return WaitForTag("SEND OK", 1000);
}

bool EspDrv::SendCmd(const __FlashStringHelper* cmd, const char* tag, unsigned long timeout, ...)
{
  char cmdBuf[CMD_BUFFER_SIZE];
  va_list args;
  va_start(args, cmd);
  vsnprintf_P(cmdBuf, CMD_BUFFER_SIZE, (char*)cmd, args);
  va_end(args);
  WaitUntilReady();
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
    tagRecognitionFailCount = tagRecognitionFailCount == 255? tagRecognitionFailCount : tagRecognitionFailCount + 1;;
  }
  else
  {
    tagRecognitionFailCount = 0;
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

  this->SendCmd(F("AT+CIPSTATUS"), "OK", 1000);
  statusFound = false;
  if(this->state == EspReadState::STATUS)
  {
    this->state = EspReadState::IDLE;
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
  lastConnectionStatus = GetConnectionStatus(true);
}

void EspDrv::Close()
{
  this->SendCmd(F("AT+CIPCLOSE"), "OK", 1000);
  lastConnectionStatus = GetConnectionStatus(true);
}

void EspDrv::Reset()
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

uint8_t EspDrv::GetMemAllocFailCount()
{
  return this->memAllocFailCount;
}

uint8_t EspDrv::GetTagRecognitionFailCount()
{
  return this->tagRecognitionFailCount;
}
