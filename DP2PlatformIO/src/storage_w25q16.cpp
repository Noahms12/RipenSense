#include "storage_w25q16.h"

StorageW25Q16::StorageW25Q16(Adafruit_FlashTransport_SPI* transport) 
    : _flash(transport) {}

bool StorageW25Q16::begin() {
    return _flash.begin();
}