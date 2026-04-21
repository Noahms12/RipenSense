#ifndef STORAGE_W25Q16_H
#define STORAGE_W25Q16_H
#include <SPI.h>
#include <Adafruit_SPIFlash.h>

class StorageW25Q16 {
public:
    StorageW25Q16(Adafruit_FlashTransport_SPI* transport);
    bool begin();
private:
    Adafruit_SPIFlash _flash;
};
#endif