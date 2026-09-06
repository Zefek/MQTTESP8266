#include "EspDrvV4.h"
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

EspDrvV4::EspDrvV4(Stream* serial) 
{
  this->serial = serial;
}

int EspDrvV4::CompareRingBuffer(const char* input)
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

void EspDrvV4::ResetBuffer(uint8_t* buffer, uint16_t length)
{
  if(buffer == nullptr)
  {
    return;
  }
  memset(buffer, 0, length);
}

void EspDrvV4::CheckTimeout()
{
  switch(this->state)
  {
    case EspV4ReadState::STATUS:
      if(millis() - statusTimer > 1000)
      {
        PRINTLN_WARNING(F("Status timout expired."));
        this->state = EspV4ReadState::IDLE;
        statusCounter = 0;
      }
    break;
    case EspV4ReadState::DATA:
    case EspV4ReadState::DATA_LENGTH:
    {
      bool interByte = millis() - startDataReadMillis > interByteTimeoutMs;
      bool cumulative = false;
      if(receivedDataLength > 0)
      {
        unsigned long expectedDataTime =
            (unsigned long)receivedDataLength * PER_BYTE_BUDGET_MS + fixedTimeoutReserveMs;
        cumulative = millis() - dataStartedMillis > expectedDataTime;
      }
      if(interByte || cumulative)
      {
        PRINTLN_WARNING(F("Data timout expired."));
        this->state = EspV4ReadState::IDLE;
        dataRead = 0;
        receivedDataLength = 0;
        ignoreReceivedData = false;
        ResetBuffer(receivedDataBuffer, receivedDataBufferSize);
        if(this->DataTimeout != nullptr)
        {
          this->DataTimeout();
        }
      }
    }
    break;
    case EspV4ReadState::BUSY:
      if(millis() - busyTime > busyTimeout)
      {
        PRINTLN_WARNING(F("Busy timout expired."));
        this->state = EspV4ReadState::IDLE;
      }
    break;
  }
}

void EspDrvV4::Loop()
{
  if(closeRequested)
  {
    closeRequested = false;
    Close();
  }
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
      case EspV4ReadState::STATUS:
        if(c >= '0' && c <= '5')
        {
          lastConnectionStatus = (int)(c - '0');
          PRINT_DEBUG("Connection status ");
          PRINTLN_DEBUG(lastConnectionStatus);
          this->state = EspV4ReadState::IDLE;
          continue;
        }
        else
        {
          statusCounter++;
          if(statusCounter > 5 || millis() - statusTimer > 1000)
          {
            statusCounter = 0;
            this->state = EspV4ReadState::IDLE;
            continue;
          }
        }
      break;
      case EspV4ReadState::CWJAP:
        if(c == '\r' || c == '\n')
        {
          if(cwjapCommaCount == 3)
          {
            lastRssi = cwjapRssiNeg ? -cwjapRssiAcc : (int8_t)cwjapRssiAcc;
            PRINT_DEBUG("RSSI ");
            PRINTLN_DEBUG(lastRssi);
          }
          this->state = EspV4ReadState::IDLE;
          continue;
        }
        if(c == '"')
        {
          cwjapInQuotes = !cwjapInQuotes;
        }
        else if(c == ',' && !cwjapInQuotes)
        {
          if(cwjapCommaCount == 3)
          {
            lastRssi = cwjapRssiNeg ? -cwjapRssiAcc : (int8_t)cwjapRssiAcc;
            PRINT_DEBUG("RSSI ");
            PRINTLN_DEBUG(lastRssi);
          }
          cwjapCommaCount++;
        }
        else if(cwjapCommaCount == 3)
        {
          if(c == '-')
          {
            cwjapRssiNeg = true;
          }
          else if(c >= '0' && c <= '9')
          {
            cwjapRssiAcc = cwjapRssiAcc * 10 + (c - '0');
          }
        }
        continue;
      case EspV4ReadState::DATA_LENGTH:
        startDataReadMillis = millis();
        if (this->receivedDataBuffer == nullptr)
        {
          PRINTLN_ERROR(F("No data buffer, requesting close."));
          dataRead = 0;
          receivedDataLength = 0;
          this->state = EspV4ReadState::IDLE;
          closeRequested = true;
          return;
        }
        if (dataRead > 6)
        {
          PRINTLN_ERROR(F("Data length too long, requesting close."));
          dataRead = 0;
          receivedDataLength = 0;
          ResetBuffer(receivedDataBuffer, receivedDataBufferSize);
          this->state = EspV4ReadState::IDLE;
          closeRequested = true;
          return;
        }
        if (c == ':')
        {
          receivedDataBuffer[dataRead++] = '\0';
          int result = sscanf((char*)receivedDataBuffer, "%hu", &receivedDataLength);
          PRINT_DEBUG("Data length ");
          PRINTLN_DEBUG(receivedDataLength);
          if(result != 1 || receivedDataLength == 0)
          {
            PRINTLN_ERROR(F("Invalid data length, requesting close."));
            dataRead = 0;
            receivedDataLength = 0;
            ResetBuffer(receivedDataBuffer, receivedDataBufferSize);
            this->state = EspV4ReadState::IDLE;
            closeRequested = true;
            return;
          }
          if(receivedDataLength > maxAllowedDataLength)
          {
            PRINT_WARNING(F("Data length exceeds limit, draining: "));
            PRINTLN_WARNING(receivedDataLength);
            dataRead = 0;
            ignoreReceivedData = true;
            this->state = EspV4ReadState::DATA;
            startDataReadMillis = millis();
            continue;
          }
          dataRead = 0;
          if(receivedDataBufferSize < receivedDataLength)
          {
            PRINT_DEBUG(F("Need data buffer allocation. Current size: "));
            PRINT_DEBUG(receivedDataBufferSize);
            PRINT_DEBUG(F(". New size: "));
            PRINTLN_DEBUG(receivedDataLength);
            // Starý buffer se uvolní PŘED alokací nového. Jeho obsah už není
            // potřeba (délka je vyparsovaná, následuje ResetBuffer), a držet
            // obě alokace zároveň je na AVR s pár stovkami bajtů haldy zbytečné
            // riziko.
            uint16_t previousSize = this->receivedDataBufferSize;
            delete[] this->receivedDataBuffer;
            this->receivedDataBuffer = new uint8_t[receivedDataLength];
            if(this->receivedDataBuffer != nullptr)
            {
              this->receivedDataBufferSize = receivedDataLength;
              memAllocFailCount = 0;
            }
            else
            {
              PRINT_ERROR(F("Error in data buffer allocation. Expected size: "));
              PRINT_ERROR(receivedDataLength);
              PRINT_ERROR(F(". Current size: "));
              PRINTLN_ERROR(previousSize);
              memAllocFailCount = memAllocFailCount == 255? memAllocFailCount : memAllocFailCount + 1;
              this->receivedDataBuffer = new uint8_t[previousSize];
              this->receivedDataBufferSize = this->receivedDataBuffer != nullptr? previousSize : 0;
              // Zprávu je nutné dočíst i bez bufferu, jinak by se zbytek
              // payloadu parsoval jako AT odpovědi a stream by se rozsypal.
              dataRead = 0;
              ignoreReceivedData = true;
              startDataReadMillis = millis();
              this->state = EspV4ReadState::DATA;
              continue;
            }
          }
          startDataReadMillis = millis();
          ResetBuffer(receivedDataBuffer, receivedDataBufferSize);
          dataRead = 0;
          this->state = EspV4ReadState::DATA;
        } 
        else 
        {
          if(c >= '0' && c <= '9')
          {
            this->receivedDataBuffer[dataRead++] = c;
          }
        }
      break;
      case EspV4ReadState::DATA:
        if(!ignoreReceivedData)
        {
          receivedDataBuffer[dataRead] = (uint8_t)raw;
        }
        dataRead++;
        startDataReadMillis = millis();
        if (dataRead == receivedDataLength)
        {
          PRINTLN_DEBUG(F("Read all received data."));
          if(ignoreReceivedData)
          {
            if(this->DataIgnored != nullptr)
            {
              this->DataIgnored(receivedDataLength);
            }
            ignoreReceivedData = false;
          }
          else
          {
            if(this->DataReceived != nullptr)
            {
              this->DataReceived(receivedDataBuffer, receivedDataLength);
            }
            ResetBuffer(receivedDataBuffer, receivedDataBufferSize);
          }
          dataRead = 0;
          receivedDataLength = 0;
          statusRead = millis();
          this->state = busyTryCount > 0? EspV4ReadState::BUSY : EspV4ReadState::IDLE;
          continue;
        }
        PRINT_TRACE(F("Read "));
        PRINT_TRACE(dataRead);
        PRINT_TRACE(F("/"));
        PRINTLN_TRACE(receivedDataLength);
        continue;
    }
    if(c >= 32 && c <= 126)
    {
      ringBuffer[ringBufferTail] = c;
      ringBufferTail = (ringBufferTail + 1) % ringBufferLength;
    }
    if((this->state == EspV4ReadState::IDLE || this->state == EspV4ReadState::BUSY) && this->expectedTag != nullptr)
    {
      if (CompareRingBuffer(this->expectedTag) == 0) 
      {
        PRINT_DEBUG(F("Tag recognized "));
        PRINTLN_DEBUG(this->expectedTag);
        TagReceived(this->expectedTag);
        this->expectedTag = nullptr;
        if(this->state == EspV4ReadState::BUSY)
        {
          busyTimeout = 0;
          busyTryCount = 0;
          this->state = EspV4ReadState::IDLE;
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
      dataStartedMillis = millis();
      this->lastState = this->state;
      this->state = EspV4ReadState::DATA_LENGTH;
    }
    else if (CompareRingBuffer("+CWJAP:") == 0 && (this->state == EspV4ReadState::IDLE || this->state == EspV4ReadState::BUSY) && !cwjapFound)
    {
      PRINTLN_DEBUG(F("+CWJAP"));
      cwjapFound = true;
      cwjapCommaCount = 0;
      cwjapRssiAcc = 0;
      cwjapRssiNeg = false;
      cwjapInQuotes = false;
      busyTimeout = 0;
      busyTryCount = 0;
      this->state = EspV4ReadState::CWJAP;
    }
    else if (CompareRingBuffer("STATUS:") == 0 && (this->state == EspV4ReadState::IDLE || this->state == EspV4ReadState::BUSY) && !statusFound)
    {
      PRINTLN_DEBUG(F("STATUS"));
      statusTimer = millis();
      statusCounter = 0;
      statusFound = true;
      busyTimeout = 0;
      busyTryCount = 0;
      this->state = EspV4ReadState::STATUS;
    }
    else if (CompareRingBuffer("CLOSED") == 0 && (this->state == EspV4ReadState::IDLE || this->state == EspV4ReadState::BUSY))
    {
      PRINTLN_DEBUG(F("CLOSED"));
      if(this->state == EspV4ReadState::BUSY)
      {
        busyTimeout = 0;
        busyTryCount = 0;
        this->state = EspV4ReadState::IDLE;
      }
      lastConnectionStatus = 4;
      statusRead = millis();
    }
    else if (CompareRingBuffer("BUSY") == 0 && this->state == EspV4ReadState::IDLE)
    {
      PRINTLN_WARNING(F("BUSY"));
      if(this->OnBusy != nullptr)
      {
        this->OnBusy(busyTryCount);
      }
      busyTryCount++;
      busyTimeout = min(busyTimeout * 2 + random(200, 1000), 5000);
      busyTime = millis();
      this->state = EspV4ReadState::BUSY;
    }
  }
}

void EspDrvV4::Init(uint8_t receivedBufferSize,
                  uint16_t maxAllowedDataLength,
                  unsigned long interByteTimeoutMs,
                  unsigned long fixedTimeoutReserveMs)
{
  this->maxAllowedDataLength = maxAllowedDataLength;
  this->interByteTimeoutMs = interByteTimeoutMs;
  this->fixedTimeoutReserveMs = fixedTimeoutReserveMs;
  if(this->SendCmd(F("ATE0"), "OK", 1000))
  {
    if(this->SendCmd(F("AT+RST"), "OK", 30000))
    {
      delay(3000);
      if(this->SendCmd(F("ATE0"), "OK", 10000))
      {
        this->SendCmd(F("AT+SYSSTORE=0"), "OK", 1000);
        this->SendCmd(F("AT+CWMODE=1"), "OK", 1000);
      }
      GetConnectionStatus(true);
    }
  }
  this->receivedDataBuffer = new uint8_t[receivedBufferSize];
  this->receivedDataBufferSize = receivedBufferSize;
}

int EspDrvV4::Connect(const char* ssid, const char* password)
{
  if(this->SendCmd(F("AT+CWJAP=\"%s\",\"%s\""), "OK", 10000, ssid, password))
  {
    delay(100);
    if(this->SendCmd(F("AT+CIPMUX=0"), "OK", 10000))
    {
      delay(100);
      int status = GetConnectionStatus(true);
      if(status == WL_CONNECTED && this->sntpServer != nullptr)
      {
        this->SendCmd(F("AT+CIPSNTPCFG=1,%d,\"%s\""), "+TIME_UPDATED", 15000,
                      (int)this->sntpTimezone, this->sntpServer);
        delay(100);
      }
      return status == WL_CONNECTED;
    }
  }
  return WL_DISCONNECTED;
}

void EspDrvV4::SetSecure(bool enabled, uint8_t authMode)
{
  this->secure = enabled;
  this->sslAuthMode = authMode;
}

void EspDrvV4::SetTimeSource(const char* server, int8_t timezone)
{
  this->sntpServer = server;
  this->sntpTimezone = timezone;
}

bool EspDrvV4::SetTime(unsigned long unixTime)
{
  return this->SendCmd(F("AT+SYSTIMESTAMP=%lu"), "OK", 1000, unixTime);
}

int EspDrvV4::TCPConnect(const char* url, int port)
{
  if(this->secure)
  {
    if(!this->SendCmd(F("AT+CIPSSLCCONF=%d,0,0"), "OK", 1000, this->sslAuthMode))
    {
      return 0;
    }
    delay(100);
    if(!this->SendCmd(F("AT+CIPSSLCSNI=\"%s\""), "OK", 1000, url))
    {
      return 0;
    }
    delay(100);
    if(this->SendCmd(F("AT+CIPSTART=\"SSL\",\"%s\",%d"), "OK", 20000, url, port))
    {
      delay(100);
      GetClientStatus(true);
    }
    return 0;
  }
  if(this->SendCmd(F("AT+CIPSTART=\"TCP\",\"%s\",%d"), "OK", 10000, url, port))
  {
    delay(100);
    GetClientStatus(true);
    return 0;
  }
  return 0;
}

bool EspDrvV4::Write(uint8_t* data, uint16_t length) 
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

void EspDrvV4::WaitUntilReady()
{
  // Druhá podmínka (1s po lastDataSend) je workaround pro timing ESP firmware:
  // po AT+CIPSEND a odeslání dat ESP může s odstupem poslat dodatečné odpovědi
  // nebo příchozí +IPD. Bez tohoto čekání by další SendCmd mohl narušit příjem.
  do
  {
    Loop();
  } while(this->state != EspV4ReadState::IDLE || millis() - lastDataSend < 1000);
}

bool EspDrvV4::SendData(uint8_t* data, uint16_t length) 
{
  WaitUntilReady();
  this->serial->write(data, length);
  return WaitForTag("SEND OK", 1000);
}

bool EspDrvV4::SendCmd(const __FlashStringHelper* cmd, const char* tag, unsigned long timeout, ...)
{
  char cmdBuf[CMD_BUFFER_SIZE];
  va_list args;
  va_start(args, timeout);
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

bool EspDrvV4::WaitForTag(const char* pTag, unsigned long timeout) 
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

void EspDrvV4::TagReceived(const char* pTag) 
{
  this->tag = pTag;
  this->expectedTag = nullptr;
}

void EspDrvV4::GetStatus(bool force)
{
  /*
  2 - GOT IP - může dojít k výpadku WiFi
  3 - TCP Connect - může dojít k výpadku tcp
  4 - TCP not conected
  5 - wifi not connected
  */
  if(millis() - statusRead < 1000 && !force && lastConnectionStatus != 5 && lastConnectionStatus != 0)
  {
    return;
  }

  this->SendCmd(F("AT+CIPSTATUS"), "OK", 1000);
  statusFound = false;
  if(this->state == EspV4ReadState::STATUS)
  {
    this->state = EspV4ReadState::IDLE;
  }
  statusRead = millis();
}

int EspDrvV4::GetConnectionStatus()
{
  return GetConnectionStatus(false);
}
int EspDrvV4::GetConnectionStatus(bool force)
{
  GetStatus(force);
  if(lastConnectionStatus == 2 || lastConnectionStatus == 3 || lastConnectionStatus == 4)
  {
    return WL_CONNECTED;
  }
  else if(lastConnectionStatus == 0 || lastConnectionStatus == 5)
  {
		return WL_DISCONNECTED;
  }
	return WL_IDLE_STATUS;
}

uint8_t EspDrvV4::GetClientStatus()
{
  return GetClientStatus(false);
}
uint8_t EspDrvV4::GetClientStatus(bool force)
{
  GetStatus(force);
  if(lastConnectionStatus == 3)
  {
    return CL_CONNECTED;
  }
  return CL_DISCONNECTED;
}

void EspDrvV4::Disconnect()
{
  this->SendCmd(F("AT+CWQAP"), "OK", 1000);
  lastConnectionStatus = GetConnectionStatus(true);
}

void EspDrvV4::Close()
{
  if(inClose)
  {
    return;
  }
  inClose = true;
  this->SendCmd(F("AT+CIPCLOSE"), "OK", 1000);
  lastConnectionStatus = GetConnectionStatus(true);
  inClose = false;
}

void EspDrvV4::Reset()
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

uint8_t EspDrvV4::GetMemAllocFailCount()
{
  return this->memAllocFailCount;
}

uint8_t EspDrvV4::GetTagRecognitionFailCount()
{
  return this->tagRecognitionFailCount;
}

int8_t EspDrvV4::GetRssi()
{
  cwjapFound = false;
  this->SendCmd(F("AT+CWJAP?"), "OK", 1000);
  if(this->state == EspV4ReadState::CWJAP)
  {
    this->state = EspV4ReadState::IDLE;
  }
  return lastRssi;
}
