#ifndef DGS2_GAS_H
#define DGS2_GAS_H
#include <Arduino.h>

class DGS2Gas {
public:
    DGS2Gas(HardwareSerial* serial);
    void begin(uint32_t baud = 9600);
    String readData(); 
private:
    HardwareSerial* _serial;
};
#endif