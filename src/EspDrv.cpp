#include "EspDrv.h"
#include <Arduino.h>
#include <avr/wdt.h>

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
    if (this->state == ESPREADSTATE_DATA_LENGTH1) 
    {
      char c = this->serial->read();
      if (c == ':') 
      {
        buffer[bufLength++] = '\0';
        sscanf(buffer, "%d", &receivedDataLength);
        bufLength = 0;
        dataRead = 0;
        if(sizeof(this->receivedDataBuffer) / sizeof(this->receivedDataBuffer[0]) < receivedDataLength)
        {
          delete(this->receivedDataBuffer);
          this->receivedDataBuffer = new uint8_t[receivedDataLength];
        }
        this->state = ESPREADSTATE_DATA2;
        startDataReadMillis = millis();
      } 
      else 
      {
        this->buffer[bufLength++] = c;
      }
    } 
    else if (this->state == ESPREADSTATE_DATA2) 
    {
      uint8_t r = this->serial->read();
      receivedDataBuffer[dataRead++] = r;
      if (dataRead == receivedDataLength) 
      {
        this->state = ESPREADSTATE1;
        DataReceived(receivedDataBuffer, dataLength);
      }
      if(millis() - startDataReadMillis > 5000)
      {
        this->state = ESPREADSTATE1;
      }
    } 
    else if (this->state == ESPREADSTATE2) 
    {
      if (this->serial->read() == '\n') 
      {
        this->state = ESPREADSTATE1;
        this->bufLength = 0;
      }
    } 
    else 
    {
      char c = this->serial->read();
      if (c == '\r') 
      {
        this->state = ESPREADSTATE2;
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
        this->state = ESPREADSTATE_DATA_LENGTH1;
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
    } 
  }
}

void EspDrv::Init(uint8_t receivedBufferSize)
{
  this->SendCmd(F("ATE0"));
  if(WaitForTag("OK", 10000))
  {
    this->SendCmd(F("AT+RST"));
    if(WaitForTag("OK", 30000))
    {
      delay(3000);
      this->SendCmd(F("ATE0"));
      if(WaitForTag("OK", 10000))
      {
        this->SendCmd(F("AT+CWMODE=1"));
        WaitForTag("OK", 1000);
      }
      lastConnectionStatus = GetConnectionStatus(true);
    }
  }
  this->receivedDataBuffer = new uint8_t[receivedBufferSize];
}

int EspDrv::Connect(const char* ssid, const char* password) 
{
  this->SendCmd(F("AT+CWJAP_CUR=\"%s\",\"%s\""), ssid, password);
  if(WaitForTag("OK", 10000))
  {
    delay(100);
    this->SendCmd(F("AT+CIPMUX=0"));
    if(WaitForTag("OK", 10000))
    {
      delay(100);
      lastConnectionStatus = GetConnectionStatus(true);
      return lastConnectionStatus == WL_CONNECTED;
    }
  }
  return WL_DISCONNECTED;
}

int EspDrv::TCPConnect(const char* url, int port) {
  this->SendCmd(F("AT+CIPSTART=\"TCP\",\"%s\",%d"), url, port);
  if(WaitForTag("OK", 10000))
  {
    delay(100);
    GetClientStatus(true);
    return 0;
  }
  return 0;
}

void EspDrv::Write(uint8_t* data, uint16_t length) 
{
  this->SendCmd(F("AT+CIPSEND=%d"), length);
  if(WaitForTag(">", 1000))
  {
    this->data = data;
    this->dataLength = length;
    SendData();
    WaitForTag("SEND OK", 1000);
    lastDataSend = millis();
  }
}

void EspDrv::SendData() {
  this->serial->write(this->data, this->dataLength);
}

void EspDrv::SendCmd(const __FlashStringHelper* cmd, ...) {
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
  Serial.println(cmdBuf);
  this->serial->println(cmdBuf);
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
    Serial.print("Expected tag ");
    Serial.print(pTag);
    Serial.print(" received tag ");
    Serial.println(tag);
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
  this->SendCmd(F("AT+CIPSTATUS"));
  if(WaitForTag("STATUS", 1000))
  {
    statusBufferLength = 0;
    writeToStatusBuffer = true;
    if(WaitForTag("OK", 1000))
    {
      writeToStatusBuffer = false;
      statusBuffer[statusBufferLength] = '\0';
      Serial.print("Status buffer ");
      Serial.println(statusBuffer);
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
  this->SendCmd(F("AT+CWQAP"));
  WaitForTag("OK", 1000);
}
