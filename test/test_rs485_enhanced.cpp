#include <Arduino.h>
#include <HardwareSerial.h>

// Enhanced RS485 Modbus soil sensor debugging
#define TX_PIN 17
#define RX_PIN 16
#define DE_RE_PIN 25

HardwareSerial RS485Serial(2);

void setup() {
    Serial.begin(115200);
    Serial.println("=== Enhanced RS485 Soil Sensor Debug ===");
    
    pinMode(DE_RE_PIN, OUTPUT);
    digitalWrite(DE_RE_PIN, LOW);  // Start in receive mode
    
    // Initialize RS485 serial
    RS485Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
    
    delay(1000);
    Serial.println("RS485 initialized - Enhanced debugging mode");
    Serial.println("Commands:");
    Serial.println("  1 - Read Moisture (0x0000)");
    Serial.println("  2 - Read Temperature (0x0001)");
    Serial.println("  3 - Read pH (0x0002)");
    Serial.println("  4 - Read EC (0x0003)");
    Serial.println("  5 - Read NPK all (0x0004-0x0006)");
    Serial.println("  6 - Scan all registers (0x0000-0x000F)");
    Serial.println("  a - Auto test sequence");
    Serial.println();
}

void enableTransmit() {
    digitalWrite(DE_RE_PIN, HIGH);
    delayMicroseconds(50);
}

void enableReceive() {
    digitalWrite(DE_RE_PIN, LOW);
    delayMicroseconds(50);
}

// Calculate Modbus CRC16
uint16_t calculateCRC(uint8_t* data, uint8_t length) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// Generic register read
bool readRegister(uint16_t reg, uint16_t count, const char* description) {
    // Build Modbus request: Address=0x01, Function=0x03, Register, Count
    uint8_t request[8];
    request[0] = 0x01;           // Device address
    request[1] = 0x03;           // Function code (Read Holding Registers)
    request[2] = (reg >> 8) & 0xFF;    // Register high byte
    request[3] = reg & 0xFF;           // Register low byte
    request[4] = (count >> 8) & 0xFF;  // Count high byte
    request[5] = count & 0xFF;         // Count low byte
    
    // Calculate CRC
    uint16_t crc = calculateCRC(request, 6);
    request[6] = crc & 0xFF;           // CRC low byte
    request[7] = (crc >> 8) & 0xFF;    // CRC high byte
    
    Serial.printf("Reading %s (reg 0x%04X, count %d)...\n", description, reg, count);
    Serial.print("Request: ");
    for (int i = 0; i < 8; i++) {
        Serial.printf("0x%02X ", request[i]);
    }
    Serial.println();
    
    // Clear receive buffer
    while (RS485Serial.available()) {
        RS485Serial.read();
    }
    
    // Send request
    enableTransmit();
    RS485Serial.write(request, 8);
    RS485Serial.flush();
    enableReceive();
    
    // Wait for response
    unsigned long timeout = millis() + 1000;
    uint8_t response[32];
    int responseLen = 0;
    
    while (millis() < timeout && responseLen < 32) {
        if (RS485Serial.available()) {
            response[responseLen++] = RS485Serial.read();
            timeout = millis() + 100;  // Extend timeout for additional bytes
        }
    }
    
    if (responseLen == 0) {
        Serial.println("ERROR: No response received");
        return false;
    }
    
    Serial.print("Response: ");
    for (int i = 0; i < responseLen; i++) {
        Serial.printf("0x%02X ", response[i]);
    }
    Serial.println();
    
    // Parse response
    if (responseLen >= 5) {
        if (response[0] == 0x01 && response[1] == 0x03) {
            uint8_t byteCount = response[2];
            if (responseLen >= (3 + byteCount + 2)) {
                Serial.printf("Data (%d bytes): ", byteCount);
                for (int i = 0; i < byteCount; i += 2) {
                    if (i + 1 < byteCount) {
                        uint16_t value = (response[3 + i] << 8) | response[3 + i + 1];
                        Serial.printf("%d ", value);
                        
                        // Special interpretation for known registers
                        if (reg == 0x0000) {
                            Serial.printf("(%.1f%% moisture) ", value / 10.0);
                        } else if (reg == 0x0001) {
                            Serial.printf("(%.1f°C) ", value / 10.0);
                        } else if (reg == 0x0002) {
                            Serial.printf("(pH %.1f) ", value / 10.0);
                        } else if (reg == 0x0003) {
                            Serial.printf("(%d µS/cm) ", value);
                        }
                    }
                }
                Serial.println();
                return true;
            }
        } else if (response[0] == 0x01 && response[1] >= 0x80) {
            Serial.printf("Modbus Error: Exception code 0x%02X\n", response[2]);
            return false;
        }
    }
    
    Serial.println("ERROR: Invalid or incomplete response");
    return false;
}

void autoTestSequence() {
    Serial.println("\n=== Auto Test Sequence ===");
    
    readRegister(0x0000, 1, "Moisture");
    delay(500);
    
    readRegister(0x0001, 1, "Temperature");
    delay(500);
    
    readRegister(0x0002, 1, "pH");
    delay(500);
    
    readRegister(0x0003, 1, "EC (Electrical Conductivity)");
    delay(500);
    
    readRegister(0x0004, 1, "Nitrogen (N)");
    delay(500);
    
    readRegister(0x0005, 1, "Phosphorus (P)");
    delay(500);
    
    readRegister(0x0006, 1, "Potassium (K)");
    delay(500);
    
    Serial.println("=== Auto Test Complete ===\n");
}

void scanAllRegisters() {
    Serial.println("\n=== Scanning Registers 0x0000 - 0x000F ===");
    
    for (uint16_t reg = 0x0000; reg <= 0x000F; reg++) {
        char desc[32];
        sprintf(desc, "Register 0x%04X", reg);
        readRegister(reg, 1, desc);
        delay(300);
    }
    
    Serial.println("=== Register Scan Complete ===\n");
}

void loop() {
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        
        if (input == "1") {
            readRegister(0x0000, 1, "Moisture");
        }
        else if (input == "2") {
            readRegister(0x0001, 1, "Temperature");
        }
        else if (input == "3") {
            readRegister(0x0002, 1, "pH");
        }
        else if (input == "4") {
            readRegister(0x0003, 1, "EC");
        }
        else if (input == "5") {
            Serial.println("Reading NPK (3 registers)...");
            readRegister(0x0004, 1, "Nitrogen (N)");
            delay(300);
            readRegister(0x0005, 1, "Phosphorus (P)");
            delay(300);
            readRegister(0x0006, 1, "Potassium (K)");
        }
        else if (input == "6") {
            scanAllRegisters();
        }
        else if (input == "a") {
            autoTestSequence();
        }
        else if (input.length() > 0) {
            Serial.println("Unknown command. Available: 1-6, a");
        }
    }
    
    delay(10);
}
