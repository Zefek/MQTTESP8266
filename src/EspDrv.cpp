#include "EspDrv.h"
#include <Arduino.h>

const char* ESPTAGS[] = {
  "OK",
  "ERROR",
  "FAIL",
  "SEND OK",
  ">"
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
        this->receivedDataBuffer = new uint8_t[receivedDataLength];
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
        delete(receivedDataBuffer);
      }
      if(millis() - startDataReadMillis > 5000)
      {
        this->state = ESPREADSTATE1;
        delete(receivedDataBuffer);
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
      if(writeToStatusBuffer)
      {
        this->statusBuffer[statusBufferLength++] = c;
      }
      for (int i = 0; i < 5; i++) 
      {
        int compare = strncmp(ESPTAGS[i], this->buffer, strlen(ESPTAGS[i]));
        if (compare == 0) 
        {
          TagReceived(ESPTAGS[i]);
          bufLength = 0;
          break;
        } 
        else if (strncmp("+IPD,", this->buffer, strlen("+IPD,")) == 0) 
        {
          this->state = ESPREADSTATE_DATA_LENGTH1;
          bufLength = 0;
        }
        else if (strncmp("CLOSED", this->buffer, strlen("CLOSED")) == 0) 
        {
          connectionState = ESP_NOTCONNECTED;
          bufLength = 0;
        }
      }
    } 
  }
}

void EspDrv::Init()
{
  this->SendCmd(F("ATE0"));
  WaitForTag("OK", 10000);
  this->tag = "";
  this->SendCmd(F("AT+RST"));
  WaitForTag("OK", 30000);
  this->tag = "";
  delay(3000);
  this->SendCmd(F("ATE0"));
  WaitForTag("OK", 10000);
  this->tag = "";
}

int EspDrv::Connect(const char* ssid, const char* password) 
{
  this->SendCmd(F("AT+CWJAP_CUR=\"%s\",\"%s\""), ssid, password);
  WaitForTag("OK", 10000);
  this->tag = "";
  delay(100);
  connectionState = ESP_CONNECTED;
  this->SendCmd(F("AT+CIPMUX=0"));
  WaitForTag("OK", 10000);
  this->tag = "";
  delay(100);
  uint8_t wifiStatus = GetConnectionStatus();
  return wifiStatus == WL_CONNECTED;
}

int EspDrv::TCPConnect(const char* url, int port) {
  this->SendCmd(F("AT+CIPSTART=\"TCP\",\"%s\",%d"), url, port);
  WaitForTag("OK", 10000);
  this->tag = "";
  delay(100);
  return 0;
}

void EspDrv::Write(uint8_t* data, uint16_t length) 
{
  this->SendCmd(F("AT+CIPSEND=%d"), length);
  WaitForTag(">", 1000);
  this->data = data;
  this->dataLength = length;
  SendData();
  WaitForTag("SEND OK", 1000);
  delay(100);
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
  bool needsToReadToStatusBuffer = writeToStatusBuffer;
  writeToStatusBuffer = false;
  Loop();
  writeToStatusBuffer = needsToReadToStatusBuffer;
  this->serial->println(cmdBuf);
}

bool EspDrv::WaitForTag(const char* pTag, unsigned long timeout) 
{
  unsigned long m = millis();
  while (strncmp(this->tag, pTag, strlen(pTag)) != 0 && millis() - m < timeout) 
  {
    this->Loop();
  }
  bool result = strncmp(this->tag, pTag, strlen(pTag)) == 0;
  this->tag = "";
  return result;
}

void EspDrv::TagReceived(const char* pTag) {
  this->tag = pTag;
}

void EspDrv::GetStatus(int* wifiStatus)
{
  statusBufferLength = 0;
  writeToStatusBuffer = true;
  this->SendCmd(F("AT+CIPSTATUS"));
  WaitForTag("OK", 10000);
  writeToStatusBuffer = false;
  this->tag = "";
  statusBuffer[statusBufferLength] = '\0';
  sscanf(statusBuffer, "STATUS:%d", wifiStatus);
}

uint8_t EspDrv::GetConnectionStatus()
{
  int wifiStatus = 255;
  GetStatus(&wifiStatus);
  if(wifiStatus == 2 || wifiStatus == 3 || wifiStatus == 4)
  {
    return WL_CONNECTED;
  }
  else if(wifiStatus == 5)
  {
		return WL_DISCONNECTED;
  }
	return WL_IDLE_STATUS;
}

uint8_t EspDrv::GetClientStatus()
{
  int wifiStatus = 255;
  GetStatus(&wifiStatus);
  if(wifiStatus == 3)
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
