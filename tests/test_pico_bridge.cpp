#include "pico_mock/Arduino.h"
#include <cassert>
#include <iostream>
uint32_t fakeMillis;
FakeSerial Serial;
FakeSPI SPI1;
#include "../pico_firmware/Pico_LoRa_P2P/Pico_LoRa_P2P.ino"

int main() {
  setup();
  assert(radioReady && SPI1.sck == 10 && SPI1.mosi == 11 && SPI1.miso == 12);
  assert(Serial.output.find("PICO_READY,P2P_CMD_V1") != std::string::npos);
  Serial.output.clear();
  Serial.input = "HELLO\nCMD,41,PING\n";
  fakeMillis = 100;
  loop();
  assert(radio.lastSent == "CMD,41,PING" && radio.transmits == 1);
  assert(Serial.output.find("PICO_TX,41,QUEUED") != std::string::npos);
  assert(Serial.output.find("PICO_TX,41,SENT") != std::string::npos);
  assert(Serial.output.find("ACK,41,OK") == std::string::npos &&
         !receivedFlag && radio.reads == 0);
  /* No ACK: unknown after 12 s, no automatic retransmission. */
  fakeMillis += 12001;
  loop();
  assert(radio.transmits == 1);
  assert(Serial.output.find("PICO_ACK_TIMEOUT,41,UNKNOWN") !=
         std::string::npos);
  Serial.output.clear();
  radio.incoming = "ACK,41,OK,STM32L476";
  receivedFlag = true;
  loop();
  assert(Serial.output.find("[RX] Payload: \"ACK,41,OK,STM32L476\"") !=
         std::string::npos);
  Serial.output.clear();
  radio.incoming.assign("\xA1\x10\x27\x00\x00\x00\x00\x00\x00", P2P_QUAT_V1_LEN);
  radio.incoming.append("|$ADSB,test\r\n\0", 14);
  assert(!receivedFlag);
  fakeDio1 = 1; // The radio asserted DIO1 but the callback was missed.
  loop();
  fakeDio1 = LOW;
  assert(Serial.output.find("[RX] Payload: \"Q,10000,0,0,0|$ADSB,test\"") !=
         std::string::npos);
  Serial.output.clear();
  radio.incoming.assign("\xA1\xFF", 2);
  receivedFlag = true;
  loop();
  assert(Serial.output.find("RX format error: truncated Q frame len=2 hex= A1 FF") !=
         std::string::npos);
  Serial.output.clear();
  const unsigned char wheelFrame[] = {0xA2, 0x10, 0x27, 0, 0, 0, 0, 0, 0,
                                     0xC4, 0x05, 0xFB, 0x53, 0xFF, 0xFF};
  radio.incoming.assign(reinterpret_cast<const char *>(wheelFrame), sizeof(wheelFrame));
  receivedFlag = true;
  loop();
  assert(Serial.output.find("Q,10000,0,0,0|WHEEL,2,3,1476,-5,65535") != std::string::npos);
  Serial.output.clear();
  const unsigned char adcsFields[] = {6, 1, 0x90, 0xE8, 125, 0, 1};
  radio.incoming.append(reinterpret_cast<const char *>(adcsFields), sizeof(adcsFields));
  receivedFlag = true;
  loop();
  assert(Serial.output.find("|ADCS,6,1,-6000,125,1") != std::string::npos);
  assert(P2P_TimeOnAirUs(P2P_WHEEL_ADCS_LEN) == 56576U);
  Serial.output.clear();
  const uint16_t faceReadings[] = {0, 2000, 4095, 256};
  uint8_t packedLight[6];
  P2P_PackLight(packedLight, faceReadings);
  radio.incoming[21] = 0x4D;
  radio.incoming.append(reinterpret_cast<const char *>(packedLight), sizeof(packedLight));
  receivedFlag = true;
  loop();
  assert(Serial.output.find("|ADCS,6,1,-6000,125,64|LIGHT,13,0,2000,4095,256") != std::string::npos);
  assert(P2P_TimeOnAirUs(P2P_WHEEL_LIGHT_LEN) == 66816U);
  Serial.output.clear();
  const uint8_t maneuverFields[] = {2, 0x3D, 0xFE, 0x3E, 0xFE, 0, 128, 0, 128};
  radio.incoming.append(reinterpret_cast<const char *>(maneuverFields), sizeof(maneuverFields));
  receivedFlag = true;
  loop();
  assert(Serial.output.find("|LIGHT,13,0,2000,4095,256|MAN,2,-451,-450,-32768,-32768")
         != std::string::npos);
  Serial.output.clear();
  radio.incoming.assign("\xA1\x10\x27\x00\x00\x00\x00\x00\x00", P2P_QUAT_V1_LEN);
  radio.incoming += "|SOLAR,2048,9659";
  receivedFlag = true;
  loop();
  assert(Serial.output.find("Q,10000,0,0,0|SOLAR,2048,9659") != std::string::npos);
  Serial.output.clear();
  radio.incoming.assign("\xA1\x10\x27\x00\x00\x00\x00\x00\x00", P2P_QUAT_V1_LEN);
  radio.incoming += "|EPS,15,0,7100,7200,7150,16000";
  receivedFlag = true;
  loop();
  assert(Serial.output.find("Q,10000,0,0,0|EPS,15,0,7100,7200,7150,16000") != std::string::npos);
  Serial.output.clear();
  radio.incoming.assign("\xA1\x10\x27\x00\x00\x00\x00\x00\x00", P2P_QUAT_V1_LEN);
  radio.incoming += "|EPSI,15,1,7100,-1200,0";
  receivedFlag = true;
  loop();
  assert(Serial.output.find("Q,10000,0,0,0|EPSI,15,1,7100,-1200,0") != std::string::npos);
  P2P_Command wheel;
  const char reverse[] = "CMD,46,WHEEL,-15";
  assert(!P2P_ParseCommand(reverse, strlen(reverse), &wheel) && wheel.value == -15);
  const char tooFast[] = "CMD,46,WHEEL,-16";
  assert(P2P_ParseCommand(tooFast, strlen(tooFast), &wheel));
  for (const char *message :
       {"Q,+10000,+0,+0,+0", "TEL,1,20,0,0,1000,0,0,0,1,2,3,10000,0,0,0,1",
        "EVT,7,CAL_GYRO,DONE,BIAS_APPLIED", "TEST_LORA"}) {
    Serial.output.clear();
    radio.incoming = message;
    receivedFlag = true;
    loop();
    assert(Serial.output.find(std::string("[RX] Payload: \"") + message +
                              "\"") != std::string::npos);
  }
  Serial.output.clear();
  radio.rxResult = -7;
  radio.incoming = "corrupt";
  receivedFlag = true;
  loop();
  assert(Serial.output.find("[RX] Payload:") == std::string::npos &&
         radioReady);
  radio.rxResult = 0;
  Serial.output.clear();
  Serial.input = "CMD,42,SET_BETA,nan\n";
  loop();
  assert(Serial.output.find("PICO_TX,42,ERR,BAD_ARGS") != std::string::npos);
  Serial.output.clear();
  Serial.input = std::string(181, 'x') + "\n";
  for (int i = 0; i < 4; ++i) {
    loop();
  }
  assert(Serial.output.find("too long") != std::string::npos);
  radio.txResult = -9;
  Serial.output.clear();
  Serial.input = "CMD,43,PING\n";
  fakeMillis += 30;
  loop();
  assert(Serial.output.find("PICO_TX,43,ERR,RADIO_-9") != std::string::npos);
  int count = radio.transmits;
  for (int i = 0; i < 20; ++i) {
    loop();
  }
  assert(radio.transmits == count);
  P2P_Command relay;
  const char relayOn[] = "CMD,44,RELAY,ON";
  assert(!P2P_ParseCommand(relayOn, strlen(relayOn), &relay));
  assert(relay.name == P2P_RELAY && relay.value == P2P_RELAY_ON);
  const char relayToggle[] = "CMD,45,RELAY,TOGGLE";
  assert(P2P_ParseCommand(relayToggle, strlen(relayToggle), &relay));
  std::cout << "Pico USB/queue/TX-vs-RX/CRC/ACK-timeout checks passed (API "
               "mock only)\n";
}
