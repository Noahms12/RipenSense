#ifndef PN5180_NFC_H
#define PN5180_NFC_H

#include <Arduino.h>

typedef enum {
    NFC_OK = 0,
    NFC_INIT_FAILED = -1,
    NFC_READ_FAILED = -2,
    NFC_NO_TAG = -3,
    NFC_CRC_ERROR = -4,
    NFC_TIMEOUT = -5
} NFC_Status;

typedef struct {
    uint8_t uid[10];           // UID/serial number of tag
    uint8_t uid_len;           // Length of UID (4, 7, or 10 bytes)
    uint8_t ndef_data[256];    // NDEF payload if present
    uint16_t ndef_len;         // Length of NDEF data
    uint32_t timestamp_ms;
} NFC_Reading;

class PN5180NFC {
public:
    // Constructor takes SPI pins (NSS, BUSY, IRQ)
    PN5180NFC(uint8_t nss_pin, uint8_t busy_pin, uint8_t irq_pin);
    
    // Initialize I2C/SPI NFC reader
    NFC_Status init();
    
    // Scan for NFC tag (non-blocking, returns immediately)
    // Returns NFC_NO_TAG if no tag present
    NFC_Status scan(NFC_Reading &reading);
    
    // Blocking scan with timeout (milliseconds)
    NFC_Status scanWithTimeout(NFC_Reading &reading, uint32_t timeout_ms);
    
    // Check if tag is present (without reading data)
    bool isTagPresent();
    
    // Write NDEF data to tag (useful for encoding shipment ID, etc.)
    NFC_Status writeNDEF(const uint8_t *ndef_data, uint16_t ndef_len);
    
    // Get last detected tag
    NFC_Status getLastReading(NFC_Reading &reading);
    
private:
    uint8_t nss_pin_;
    uint8_t busy_pin_;
    uint8_t irq_pin_;
    bool initialized;
    NFC_Reading lastReading;
    
    // Helper: SPI communication
    void write(uint8_t *data, uint8_t len);
    void read(uint8_t *data, uint8_t len);
    
    // Helper: PN5180-specific commands
    NFC_Status setRFField(bool on);
    NFC_Status readRegister(uint8_t reg, uint32_t &value);
    NFC_Status writeRegister(uint8_t reg, uint32_t value);
};

#endif // PN5180_NFC_H
