#ifndef PICO_TEST_RADIOLIB_H
#define PICO_TEST_RADIOLIB_H
#include "Arduino.h"
#include <cassert>
#define RADIOLIB_DEFAULT_SPI_SETTINGS 0
#define RADIOLIB_SX126X_SYNC_WORD_PUBLIC 0x34
#define RADIOLIB_ERR_NONE 0
struct Module {
  Module(int a, int b, int c, int d, FakeSPI &spi, int) {
    assert(a == 3 && b == 20 && c == 15 && d == 2 && &spi == &SPI1);
  }
};
extern volatile bool receivedFlag;
struct SX1262 {
  void (*callback)() = nullptr;
  std::string incoming, lastSent;
  int rxResult = 0, txResult = 0, transmits = 0, reads = 0, rearms = 0;
  SX1262(Module *module) { delete module; }
  int begin(float freq, float bw, int sf, int cr, int sync, int power,
            int preamble, float tcxo) {
    assert(freq == 868.1f && bw == 125 && sf == 7 && cr == 5 && sync == 0x34 &&
           power == 10 && preamble == 8 && tcxo == 1.7f);
    return 0;
  }
  void clearDio1Action() { callback = nullptr; }
  void setDio1Action(void (*cb)()) { callback = cb; }
  int setDio2AsRfSwitch(bool b) {
    assert(b);
    return 0;
  }
  int setSyncWord(int s) {
    assert(s == 0x34);
    return 0;
  }
  int setCRC(bool b) {
    assert(b);
    return 0;
  }
  int invertIQ(bool b) {
    assert(!b);
    return 0;
  }
  int startReceive() {
    rearms++;
    return 0;
  }
  size_t getPacketLength() { return incoming.size(); }
  int readData(uint8_t *data, size_t n) {
    reads++;
    assert(n == incoming.size());
    memcpy(data, incoming.data(), n);
    return rxResult;
  }
  float getRSSI() { return -70; }
  float getSNR() { return 9; }
  float getFrequencyError() { return 0; }
  int standby() { return 0; }
  int transmit(const uint8_t *data, size_t n) {
    assert(!callback);
    transmits++;
    lastSent.assign((const char *)data, n);
    receivedFlag = true; /* Inject TX_DONE; sketch must clear before RX. */
    fakeMillis += 100;
    return txResult;
  }
};
#endif
