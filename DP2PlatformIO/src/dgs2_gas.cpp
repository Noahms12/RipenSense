#include "dgs2_gas.h"

DGS2Gas::DGS2Gas(HardwareSerial* serial) : _serial(serial) {}

void DGS2Gas::begin(uint32_t baud) {
    _serial->begin(baud);
}

String DGS2Gas::readData() {
    String data = "";
    while (_serial->available()) {
        data += (char)_serial->read();
    }
    return data;
}