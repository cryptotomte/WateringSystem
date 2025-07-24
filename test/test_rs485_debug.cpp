#include <Arduino.h>
#include <HardwareSerial.h>

// Detailed RS485 signal analysis and debugging
#define TX_PIN 17
#define RX_PIN 16
#define DE_RE_PIN 25

HardwareSerial RS485Serial(2);

void setup() {
    Serial.begin(115200);
    Serial.println("=== RS485 Signal Analysis ===");
    
    pinMode(DE_RE_PIN, OUTPUT);
    pinMode(TX_PIN, OUTPUT);
    pinMode(RX_PIN, INPUT);
    
    // Start in receive mode
    digitalWrite(DE_RE_PIN, LOW);
    
    Serial.println("Pin states:");
    Serial.printf("DE/RE (GPIO25): %d\n", digitalRead(DE_RE_PIN));
    Serial.printf("TX (GPIO17): %d\n", digitalRead(TX_PIN));
    Serial.printf("RX (GPIO16): %d\n", digitalRead(RX_PIN));
    
    RS485Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
    delay(1000);
}

void testPinToggle() {
    Serial.println("\n--- Testing Pin Toggle ---");
    
    // Test DE/RE control
    Serial.println("Testing DE/RE pin:");
    for (int i = 0; i < 5; i++) {
        digitalWrite(DE_RE_PIN, HIGH);
        Serial.printf("DE/RE HIGH: %d\n", digitalRead(DE_RE_PIN));
        delay(500);
        
        digitalWrite(DE_RE_PIN, LOW);
        Serial.printf("DE/RE LOW: %d\n", digitalRead(DE_RE_PIN));
        delay(500);
    }
}

void testSerialLoopback() {
    Serial.println("\n--- Testing Serial Loopback ---");
    
    // Enable transmit mode
    digitalWrite(DE_RE_PIN, HIGH);
    delay(10);
    
    // Send test data
    RS485Serial.write("TEST");
    RS485Serial.flush();
    
    // Switch to receive mode
    digitalWrite(DE_RE_PIN, LOW);
    delay(10);
    
    // Check for any received data
    if (RS485Serial.available()) {
        Serial.print("Loopback received: ");
        while (RS485Serial.available()) {
            Serial.print((char)RS485Serial.read());
        }
        Serial.println();
    } else {
        Serial.println("No loopback data received");
    }
}

void scanModbusDevices() {
    Serial.println("\n--- Scanning Modbus Devices ---");
    
    for (uint8_t addr = 1; addr <= 10; addr++) {
        Serial.printf("Trying address 0x%02X... ", addr);
        
        // Read input register request
        uint8_t request[8];
        request[0] = addr;          // Device address
        request[1] = 0x03;          // Function code (read holding registers)
        request[2] = 0x00;          // Start address high
        request[3] = 0x00;          // Start address low
        request[4] = 0x00;          // Quantity high
        request[5] = 0x01;          // Quantity low
        
        // Calculate CRC
        uint16_t crc = calculateCRC(request, 6);
        request[6] = crc & 0xFF;
        request[7] = (crc >> 8) & 0xFF;
        
        // Send request
        digitalWrite(DE_RE_PIN, HIGH);
        delayMicroseconds(50);
        RS485Serial.write(request, 8);
        RS485Serial.flush();
        digitalWrite(DE_RE_PIN, LOW);
        delayMicroseconds(50);
        
        // Wait for response
        unsigned long timeout = millis() + 500;
        bool responseReceived = false;
        
        while (millis() < timeout && !responseReceived) {
            if (RS485Serial.available()) {
                Serial.print("Response: ");
                while (RS485Serial.available()) {
                    Serial.printf("0x%02X ", RS485Serial.read());
                }
                Serial.println();
                responseReceived = true;
            }
        }
        
        if (!responseReceived) {
            Serial.println("No response");
        }
        
        delay(100);
    }
}

uint16_t calculateCRC(uint8_t *data, uint8_t length) {
    uint16_t crc = 0xFFFF;
    
    for (uint8_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void loop() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command == "toggle") {
            testPinToggle();
        } else if (command == "loopback") {
            testSerialLoopback();
        } else if (command == "scan") {
            scanModbusDevices();
        } else if (command == "help") {
            Serial.println("\nCommands:");
            Serial.println("toggle - Test DE/RE pin toggle");
            Serial.println("loopback - Test serial loopback");
            Serial.println("scan - Scan for Modbus devices");
            Serial.println("help - Show this help");
        } else {
            Serial.println("Unknown command. Type 'help' for commands.");
        }
    }
    
    delay(100);
}
