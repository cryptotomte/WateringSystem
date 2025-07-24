#include <Arduino.h>
#include <HardwareSerial.h>

// RS485 Modbus soil sensor test
#define TX_PIN 17
#define RX_PIN 16
#define DE_RE_PIN 25

HardwareSerial RS485Serial(2);

void setup() {
    Serial.begin(115200);
    Serial.println("RS485 Soil Sensor Test");
    
    pinMode(DE_RE_PIN, OUTPUT);
    digitalWrite(DE_RE_PIN, LOW);  // Start in receive mode
    
    // Initialize RS485 serial
    RS485Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
    
    delay(1000);
    Serial.println("RS485 initialized - Testing communication");
}

void enableTransmit() {
    digitalWrite(DE_RE_PIN, HIGH);
    delayMicroseconds(50);  // Stabilization delay
}

void enableReceive() {
    digitalWrite(DE_RE_PIN, LOW);
    delayMicroseconds(50);  // Stabilization delay
}

// Read soil moisture (register 0x0000)
void readSoilMoisture() {
    uint8_t request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
    
    Serial.println("Sending Modbus request...");
    
    // Send request
    enableTransmit();
    RS485Serial.write(request, sizeof(request));
    RS485Serial.flush();
    enableReceive();
    
    // Wait for response
    unsigned long timeout = millis() + 1000;
    while (millis() < timeout) {
        if (RS485Serial.available()) {
            Serial.print("Response: ");
            while (RS485Serial.available()) {
                uint8_t byte = RS485Serial.read();
                Serial.print("0x");
                if (byte < 16) Serial.print("0");
                Serial.print(byte, HEX);
                Serial.print(" ");
            }
            Serial.println();
            return;
        }
    }
    Serial.println("No response received");
}

void loop() {
    readSoilMoisture();
    delay(5000);
}
