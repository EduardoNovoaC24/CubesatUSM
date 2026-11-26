#include <Wire.h>

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
}

void loop() {
  Wire.beginTransmission(0x40);

  if (Wire.endTransmission() == 0) {
    Serial.println("INA219 conectado correctamente");
  } else {
    Serial.println("INA219 no responde");
  }

  delay(1000);
}
