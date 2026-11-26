/* Arduino-Pico + RadioLib. Pico/Pico 2 W with Waveshare Pico-LoRa-SX1262.
 * Copy this entire folder (including p2p_protocol.h/.c) into Arduino IDE.
 * USB HELLO / CMD -> RF CMD; real RF ACK/EVT/TEL -> original [RX] lines.
 */
#include "p2p_protocol.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <cstdio>
#include <cstring>

void setFlag();
static void reportTx(uint16_t id, const char *state, const char *reason = nullptr);
static bool armReceive();
static bool initializeRadio();
static void serialCommand(const char *line, size_t length);
static void pollSerial();
static void trackSent(uint16_t id);
static void observeAck(const char *payload);
static const char *decodePayload(uint8_t *data, size_t length);
static void receivePacket();
static void transmitQueued();
static void transmitLinkFeedback();
void setup();
void loop();

static constexpr int radioDio1 = 20;
SX1262 radio = new Module(3, radioDio1, 15, 2, SPI1, RADIOLIB_DEFAULT_SPI_SETTINGS);
volatile bool receivedFlag = false;
static bool radioReady;
static unsigned recoveryAttempts;
static uint32_t lastRecovery, lastPacketTime, lastNotice, lastRadioAction;
static char serialLine[P2P_SERIAL_MAX + 1];
static size_t serialLength;
static bool serialOverflow;
struct QueuedCommand {
  uint16_t id;
  char line[P2P_SERIAL_MAX + 1];
};
static QueuedCommand commands[4];
static unsigned queueHead, queueCount;
struct AwaitingAck {
  uint16_t id;
  uint32_t sentAt;
};
static AwaitingAck awaiting[8];
static P2P_AirtimeBudget txBudget;
static bool linkPending;
static int linkRssi;

void setFlag() { receivedFlag = true; }
static void reportTx(uint16_t id, const char *state, const char *reason) {
  Serial.print("PICO_TX,");
  Serial.print(id);
  Serial.print(',');
  Serial.print(state);
  if (reason) {
    Serial.print(',');
    Serial.print(reason);
  }
  Serial.println();
}
static bool armReceive() {
  receivedFlag = false;
  radio.setDio1Action(setFlag);
  int state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[PICO] startReceive error=");
    Serial.println(state);
    radio.clearDio1Action();
    radioReady = false;
    return false;
  }
  return true;
}
static bool initializeRadio() {
  radio.clearDio1Action();
  receivedFlag = false;
  int state = radio.begin(868.1, 125.0, P2P_LORA_SF, 5, RADIOLIB_SX126X_SYNC_WORD_PUBLIC,
                          10, P2P_LORA_PREAMBLE, 1.7);
  if (state == RADIOLIB_ERR_NONE)
    state = radio.setDio2AsRfSwitch(true);
  if (state == RADIOLIB_ERR_NONE)
    state = radio.setSyncWord(RADIOLIB_SX126X_SYNC_WORD_PUBLIC);
  if (state == RADIOLIB_ERR_NONE)
    state = radio.setCRC(true);
  if (state == RADIOLIB_ERR_NONE)
    state = radio.invertIQ(false);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[PICO] Radio init error=");
    Serial.println(state);
    return false;
  }
  radioReady = true;
  if (!armReceive())
    return false;
  lastRadioAction = millis();
  Serial.println(
      "[PICO] SPI1 GP10/11/12; NSS3 BUSY2 RESET15 DIO1=20; TCXO=1.7V");
  Serial.println(
      "[PICO] 868.1MHz SF7 BW125 CR4/5 preamble8 sync3444 CRC ON IQ normal");
  Serial.println("PICO_READY,P2P_CMD_V1");
  return true;
}
static void serialCommand(const char *line, size_t length) {
  if (length == 5 && !memcmp(line, "HELLO", 5)) {
    Serial.println(radioReady ? "PICO_READY,P2P_CMD_V1" : "PICO_NOT_READY");
    return;
  }
  P2P_Command parsed;
  const char *error = P2P_ParseCommand(line, length, &parsed);
  if (error) {
    if (parsed.id)
      reportTx(parsed.id, "ERR", error);
    else
      Serial.println("[PICO] Invalid command header/length");
    return;
  }
  if (!radioReady) {
    reportTx(parsed.id, "ERR", "NOT_READY");
    return;
  }
  if (queueCount == 4) {
    reportTx(parsed.id, "ERR", "QUEUE_FULL");
    return;
  }
  QueuedCommand &entry = commands[(queueHead + queueCount) % 4];
  entry.id = parsed.id;
  memcpy(entry.line, line, length);
  entry.line[length] = 0;
  queueCount++;
  reportTx(parsed.id, "QUEUED");
}
static void pollSerial() {
  unsigned processed = 0;
  while (Serial.available() && processed++ < 64) {
    int ch = Serial.read();
    if (ch == '\r')
      continue;
    if (ch == '\n') {
      if (serialOverflow)
        Serial.println("[PICO] Serial line too long; discarded");
      else if (serialLength) {
        serialLine[serialLength] = 0;
        serialCommand(serialLine, serialLength);
      }
      serialLength = 0;
      serialOverflow = false;
    } else if (!serialOverflow) {
      if (ch < 32 || ch > 126 || serialLength == P2P_SERIAL_MAX)
        serialOverflow = true;
      else
        serialLine[serialLength++] = (char)ch;
    }
  }
}
static void trackSent(uint16_t id) {
  for (auto &slot : awaiting)
    if (!slot.id || slot.id == id) {
      slot.id = id;
      slot.sentAt = millis();
      return;
    }
  /* GUI remains authoritative about the 12 s timeout; never fabricate ACK. */
  Serial.println("[PICO] ACK tracking full; GUI must track this request");
}
static void observeAck(const char *payload) {
  unsigned id = 0;
  if (sscanf(payload, "ACK,%u,", &id) == 1) {
    for (auto &slot : awaiting)
      if (slot.id == id)
        slot.id = 0;
  }
}
static const char *decodePayload(uint8_t *data, size_t length) {
  if (data[0] == P2P_WHEEL_V1) {
    const bool maneuverFrame = length == P2P_WHEEL_MANEUVER_LEN;
    const bool lightFrame = length == P2P_WHEEL_LIGHT_LEN || maneuverFrame;
    const unsigned maneuver = maneuverFrame ? data[28] : 0U;
    int angles[4] = {};
    if (maneuverFrame) {
      for (size_t i = 0; i < 4; ++i) {
        const unsigned raw = data[29 + 2 * i] | (unsigned(data[30 + 2 * i]) << 8);
        angles[i] = raw < 32768 ? int(raw) : int(raw) - 65536;
      }
    }
    const bool adcsFrame = length == P2P_WHEEL_ADCS_LEN || lightFrame;
    if (length != P2P_WHEEL_V1_LEN && !adcsFrame)
      return "invalid wheel frame length";
    const unsigned pulse = data[9] | (unsigned(data[10]) << 8);
    const int level = data[11] < 128 ? data[11] : int(data[11]) - 256;
    const unsigned flags = data[12];
    const unsigned rpm = data[13] | (unsigned(data[14]) << 8);
    unsigned axis = 0, result = 0, elapsed = 0, status = 0;
    uint16_t light[P2P_LIGHT_CHANNELS] = {};
    int bodyRate = 0;
    if (adcsFrame) {
      axis = data[15];
      result = data[16];
      const unsigned rawRate = data[17] | (unsigned(data[18]) << 8);
      bodyRate = rawRate < 32768 ? int(rawRate) : int(rawRate) - 65536;
      elapsed = data[19] | (unsigned(data[20]) << 8);
      status = data[21];
      if (lightFrame) P2P_UnpackLight(light, data + 22);
    }
    int q[4];
    for (size_t i = 0; i < 4; ++i) {
      const unsigned value = data[1 + 2 * i] | (unsigned(data[2 + 2 * i]) << 8);
      q[i] = value < 32768 ? int(value) : int(value) - 65536;
      if (q[i] < -10000 || q[i] > 10000)
        return "wheel Q component outside range";
    }
    char *output = (char *)data;
    const size_t prefix = flags & P2P_WHEEL_QUAT_VALID
      ? snprintf(output, P2P_RF_MAX + 32, "Q,%d,%d,%d,%d", q[0], q[1], q[2], q[3])
      : snprintf(output, P2P_RF_MAX + 32, "QERR,NO_DATA");
    // Capture the binary fields before expanding the payload in-place.
    const size_t wheelPrefix = snprintf(output + prefix, P2P_RF_MAX + 32 - prefix,
        "|WHEEL,%u,%u,%u,%d,%u", (flags >> 5) & 3U, flags & 7U, pulse, level, rpm);
    if (adcsFrame) {
      const size_t offset = prefix + wheelPrefix;
      const size_t adcsLength = snprintf(output + offset, P2P_RF_MAX + 32 - offset,
          "|ADCS,%u,%u,%d,%u,%u", axis, result, bodyRate, elapsed,
          lightFrame ? status & 0xF0U : status);
      if (lightFrame) {
        const size_t lightOffset = offset + adcsLength;
        const size_t lightLength = snprintf(output + lightOffset, P2P_RF_MAX + 32 - lightOffset,
                 "|LIGHT,%u,%u,%u,%u,%u", status & 15U, unsigned(light[0]),
                 unsigned(light[1]), unsigned(light[2]), unsigned(light[3]));
        if (maneuverFrame) {
          const size_t start = lightOffset + lightLength;
          snprintf(output + start, P2P_RF_MAX + 32 - start, "|MAN,%u,%d,%d,%d,%d",
                   maneuver, angles[0], angles[1], angles[2], angles[3]);
        }
      }
    }
    return nullptr;
  }
  const bool binaryQuat = data[0] == P2P_QUAT_V1;
  const size_t textStart = binaryQuat ? P2P_QUAT_V1_LEN : 0;
  if (length < textStart)
    return "truncated Q frame";

  // Older senders can include their C string terminator or line ending.
  size_t textLength = length;
  while (textLength > textStart &&
         (data[textLength - 1] == 0 || data[textLength - 1] == '\r' ||
          data[textLength - 1] == '\n'))
    --textLength;
  if (binaryQuat && textLength > textStart && data[textStart] != '|')
    return "invalid Q suffix separator";
  if (textLength == 0)
    return "empty text frame";
  for (size_t i = textStart; i < textLength; ++i)
    if (data[i] < 32 || data[i] > 126)
      return binaryQuat ? "non-printable Q suffix" : "unknown binary format or invalid text";

  if (binaryQuat) {
    int q[4];
    for (size_t i = 0; i < 4; ++i) {
      unsigned value = data[1 + 2 * i] | (unsigned(data[2 + 2 * i]) << 8);
      q[i] = value < 32768 ? int(value) : int(value) - 65536;
      if (q[i] < -10000 || q[i] > 10000)
        return "Q component outside range";
    }
    char prefix[32];
    const size_t prefixLength = snprintf(prefix, sizeof(prefix), "Q,%d,%d,%d,%d",
                                         q[0], q[1], q[2], q[3]);
    const size_t suffixLength = textLength - textStart;
    memmove(data + prefixLength, data + textStart, suffixLength);
    memcpy(data, prefix, prefixLength);
    textLength = prefixLength + suffixLength;
  }
  data[textLength] = 0;
  return nullptr;
}
static void receivePacket() {
  if (!radioReady || (!receivedFlag && digitalRead(radioDio1) == LOW))
    return;
  receivedFlag = false;
  size_t length = radio.getPacketLength();
  if (!length || length > P2P_RF_MAX) {
    armReceive();
    Serial.println("[PICO] Invalid RF length; packet discarded");
  } else {
    uint8_t data[P2P_RF_MAX + 32] = {}; // Space to expand the quaternion for USB.
    int state = radio.readData(data, length);
    const uint32_t receivedAt = millis();
    if (state == RADIOLIB_ERR_NONE) {
      const float rssi = radio.getRSSI(), snr = radio.getSNR(),
                  frequencyError = radio.getFrequencyError();
      armReceive();
      const char *decodeError = decodePayload(data, length);
      if (!decodeError) {
        if ((!strncmp((char *)data, "TEL,", 4) ||
             !strncmp((char *)data, "QERR,", 5) ||
             !strncmp((char *)data, "$ADSB,", 6) ||
             !strncmp((char *)data, "Q,", 2)) && rssi >= -130 && rssi <= 0) {
          linkRssi = (int)rssi;
          linkPending = true;
          lastRadioAction = receivedAt;
        }
        Serial.println("[RX] ------------------------------------------");
        Serial.print("[RX] Payload: \"");
        Serial.print((char *)data);
        Serial.println("\"");
        Serial.print("[RX] Longitud: ");
        Serial.print(length);
        Serial.println(" bytes");
        Serial.print("[RX] RSSI: ");
        Serial.print(rssi);
        Serial.println(" dBm");
        Serial.print("[RX] SNR: ");
        Serial.print(snr);
        Serial.println(" dB");
        Serial.print("[RX] Freq error: ");
        Serial.print(frequencyError);
        Serial.println(" Hz");
        Serial.println("[RX] ------------------------------------------");
        observeAck((char *)data);
        lastPacketTime = receivedAt;
        recoveryAttempts = 0;
      } else {
        Serial.print("[PICO] RX format error: ");
        Serial.print(decodeError);
        Serial.print(" len=");
        Serial.print(length);
        Serial.print(" hex=");
        for (size_t i = 0; i < length && i < 16; ++i) {
          char byteHex[4];
          snprintf(byteHex, sizeof(byteHex), " %02X", unsigned(data[i]));
          Serial.print(byteHex);
        }
        Serial.println();
      }
    } else {
      armReceive();
      Serial.print("[PICO] RX discarded; RadioLib error=");
      Serial.println(state);
    }
  }
}
static void transmitQueued() {
  if (!radioReady || !queueCount || receivedFlag || digitalRead(radioDio1) != LOW ||
      millis() - lastRadioAction < 25)
    return;
  QueuedCommand &entry = commands[queueHead];
  size_t length = strlen(entry.line);
  if (!P2P_BudgetTake(&txBudget, millis(), length, true))
    return;
  /* TX uses RadioLib's bounded blocking transmit. Detach RX IRQ handling so
   * TX_DONE cannot be mistaken for a received command/ACK. */
  radio.clearDio1Action();
  receivedFlag = false;
  int state = radio.standby();
  if (state == RADIOLIB_ERR_NONE)
    state = radio.transmit((const uint8_t *)entry.line, length);
  receivedFlag = false;
  bool rxOk = armReceive();
  lastRadioAction = millis();
  if (state == RADIOLIB_ERR_NONE) {
    reportTx(entry.id, "SENT");
    trackSent(entry.id);
    if (!rxOk)
      Serial.println(
          "[PICO] TX sent but RX rearm failed; ACK reception unavailable");
  } else {
    char reason[32];
    snprintf(reason, sizeof(reason), "RADIO_%d", state);
    reportTx(entry.id, "ERR", reason);
  }
  queueHead = (queueHead + 1) % 4;
  queueCount--; /* Never auto-retry commands. */
}
static void transmitLinkFeedback() {
  const uint32_t now = millis();
  if (!radioReady || !linkPending || queueCount || receivedFlag ||
      digitalRead(radioDio1) != LOW || now - lastRadioAction < 40)
    return;
  char frame[16];
  size_t length = snprintf(frame, sizeof(frame), "LINK,%d", linkRssi);
  if (!P2P_BudgetTake(&txBudget, now, length, false))
    return;
  linkPending = false;
  radio.clearDio1Action();
  receivedFlag = false;
  if (radio.standby() == RADIOLIB_ERR_NONE)
    radio.transmit((const uint8_t *)frame, length);
  armReceive();
  lastRadioAction = millis();
}
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000)
    delay(1);
  SPI1.setSCK(10);
  SPI1.setTX(11);
  SPI1.setRX(12);
  SPI1.begin();
  P2P_BudgetInit(&txBudget, millis());
  radioReady = initializeRadio();
  lastRecovery = lastPacketTime = lastNotice = millis();
}
void loop() {
  receivePacket();
  pollSerial();
  transmitQueued();
  transmitLinkFeedback();
  uint32_t now = millis();
  for (auto &slot : awaiting)
    if (slot.id && now - slot.sentAt >= 12000) {
      Serial.print("PICO_ACK_TIMEOUT,");
      Serial.print(slot.id);
      Serial.println(",UNKNOWN");
      slot.id = 0; /* No claim of STM32 failure/success and no command retry. */
    }
  if (!radioReady && recoveryAttempts < 3 && now - lastRecovery >= 5000) {
    recoveryAttempts++;
    lastRecovery = now;
    radioReady = initializeRadio();
  }
  if (now - lastPacketTime >= 15000 && now - lastNotice >= 15000) {
    Serial.println("[PICO] Esperando TEL/Q/ACK/EVT de la STM32...");
    lastNotice = now;
  }
  delay(5);
}
