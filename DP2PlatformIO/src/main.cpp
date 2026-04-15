#include <Arduino.h>
#include "sht31_climate.h"
#include <bluefruit.h>

// Create the Bluetooth UART service object
BLEUart bleuart; 

void setup() {
  // 1. Initialize Bluetooth
  Bluefruit.begin();
  Bluefruit.setTxPower(4); // Set max transmission power
  Bluefruit.setName("Climate_Node"); // This is the name you'll see on your phone
  
  bleuart.begin(); // Start the UART service

  // 2. Set up Advertising so your phone can find it
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.start(0); // 0 = keep advertising forever

  // 3. Initialize your custom climate sensor
  Climate_Init();
}

void loop() {
  // Only read and send data if a phone is actually connected
  if (Bluefruit.connected()) {
    
    // Fetch data using the wrapper functions you wrote
    float temp = Climate_ReadTemp();
    float humidity = Climate_ReadHumidity();

    // Send data over Bluetooth
    bleuart.print("Temp: ");
    bleuart.print(temp);
    bleuart.println(" C");

    bleuart.print("Humidity: ");
    bleuart.print(humidity);
    bleuart.println(" %");
    bleuart.println("--------------------");
  }
  
  delay(2000); // Wait 2 seconds between updates
}
