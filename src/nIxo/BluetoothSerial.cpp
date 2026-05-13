#include "BluetoothSerial.h"

BluetoothSerial SerialBT;

const int relayPin = 23;  // 릴레이 연결 핀 (원하는 GPIO로 변경)

void setup() {
  Serial.begin(115200);
  SerialBT.begin("ESP32_NIXO"); // 블루투스 이름

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW); // 초기 OFF
}

void loop() {
  if (SerialBT.available()) {
    char incoming = SerialBT.read();

    if (incoming == 'f') {
      Serial.println("Relay ON");
      digitalWrite(relayPin, HIGH);

      delay(3000); // 3초 유지

      digitalWrite(relayPin, LOW);
      Serial.println("Relay OFF");
    }
  }
}