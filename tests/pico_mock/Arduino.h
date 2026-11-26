#ifndef PICO_TEST_ARDUINO_H
#define PICO_TEST_ARDUINO_H
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
extern uint32_t fakeMillis;
inline constexpr int LOW = 0;
inline int fakeDio1 = LOW;
inline int digitalRead(int) { return fakeDio1; }
inline uint32_t millis() { return fakeMillis; }
inline void delay(unsigned ms) { fakeMillis += ms; }
struct FakeSerial {
  std::string input, output;
  void begin(unsigned) {}
  explicit operator bool() const { return true; }
  int available() { return (int)input.size(); }
  int read() {
    unsigned char c = input.front();
    input.erase(0, 1);
    return c;
  }
  template <class T> void print(T value) {
    std::ostringstream s;
    s << value;
    output += s.str();
  }
  void println() { output += '\n'; }
  template <class T> void println(T value) {
    print(value);
    println();
  }
};
extern FakeSerial Serial;
struct FakeSPI {
  int sck = -1, mosi = -1, miso = -1;
  void setSCK(int x) { sck = x; }
  void setTX(int x) { mosi = x; }
  void setRX(int x) { miso = x; }
  void begin() {}
};
extern FakeSPI SPI1;
#endif
