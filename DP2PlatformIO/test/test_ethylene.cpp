#include <Arduino.h>

static const uint32_t BAUD_PC = 115200;
static const uint32_t BAUD_SENSOR = 9600;

void setup() {
  Serial.begin(BAUD_PC);
  Serial1.begin(BAUD_SENSOR);

  delay(1000);
  Serial.println("DGS-EC POLL TEST");
}

void loop() {
  // send poll command
  Serial1.print("C\r");

  delay(200); // allow response time

  // read full response
  while (Serial1.available()) {
    char c = Serial1.read();
    Serial.write(c);
  }

  Serial.println();

  delay(1000);
}