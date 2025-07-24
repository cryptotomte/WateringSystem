#include <Arduino.h>

// Test TXS0108E level shifter functionality with multimeter
#define TX_PIN 17
#define RX_PIN 16  
#define DE_RE_PIN 25

void setup() {
    Serial.begin(115200);
    Serial.println("=== TXS0108E Level Shifter Test ===");
    Serial.println("Use multimeter to measure voltages");
    
    pinMode(TX_PIN, OUTPUT);
    pinMode(RX_PIN, INPUT);
    pinMode(DE_RE_PIN, OUTPUT);
    
    Serial.println("\n--- Power Supply Test ---");
    Serial.println("Measure VCCA (3.3V side) and VCCB (5V side)");
    Serial.println("Press any key to continue...");
    while (!Serial.available()) delay(100);
    Serial.read();
    
    Serial.println("\n--- Static Pin Test ---");
    Serial.println("Setting all pins LOW");
    digitalWrite(TX_PIN, LOW);
    digitalWrite(DE_RE_PIN, LOW);
    
    Serial.println("Measure:");
    Serial.println("- GPIO25 (DE/RE): Should be ~0V");
    Serial.println("- GPIO17 (TX): Should be ~0V");
    Serial.println("- TXS0108E B3 (5V side DE/RE): Should be ~0V");
    Serial.println("- TXS0108E B1 (5V side TX): Should be ~0V");
    Serial.println("Press Enter when done measuring...");
    
    while (!Serial.available()) delay(100);
    while (Serial.available()) Serial.read(); // Clear buffer
    
    Serial.println("\n--- Setting all pins HIGH ---");
    digitalWrite(TX_PIN, HIGH);
    digitalWrite(DE_RE_PIN, HIGH);
    
    Serial.println("Measure:");
    Serial.println("- GPIO25 (DE/RE): Should be ~3.3V");
    Serial.println("- GPIO17 (TX): Should be ~3.3V");
    Serial.println("- TXS0108E B3 (5V side DE/RE): Should be ~5V");
    Serial.println("- TXS0108E B1 (5V side TX): Should be ~5V");
    Serial.println("Press Enter when done measuring...");
    
    while (!Serial.available()) delay(100);
    while (Serial.available()) Serial.read(); // Clear buffer
}

void loop() {
    Serial.println("\n=== Interactive Test Mode ===");
    Serial.println("Commands:");
    Serial.println("1 - Set DE/RE HIGH");
    Serial.println("2 - Set DE/RE LOW");
    Serial.println("3 - Set TX HIGH");
    Serial.println("4 - Set TX LOW");
    Serial.println("5 - Toggle DE/RE slowly");
    Serial.println("6 - Toggle TX slowly");
    Serial.println("r - Read pin states");
    Serial.println("Enter command and press Enter:");
    
    while (!Serial.available()) delay(100);
    char command = Serial.read();
    while (Serial.available()) Serial.read(); // Clear newline and extra chars
    
    switch (command) {
        case '1':
            Serial.println("Setting DE/RE HIGH...");
            digitalWrite(DE_RE_PIN, HIGH);
            Serial.println("Done. Measure B3 pin on TXS0108E (should be ~5V)");
            Serial.println("Press Enter to continue...");
            while (!Serial.available()) delay(100);
            while (Serial.available()) Serial.read();
            break;
            
        case '2':
            Serial.println("Setting DE/RE LOW...");
            digitalWrite(DE_RE_PIN, LOW);
            Serial.println("Done. Measure B3 pin on TXS0108E (should be ~0V)");
            Serial.println("Press Enter to continue...");
            while (!Serial.available()) delay(100);
            while (Serial.available()) Serial.read();
            break;
            
        case '3':
            Serial.println("Setting TX HIGH...");
            digitalWrite(TX_PIN, HIGH);
            Serial.println("Done. Measure B1 pin on TXS0108E (should be ~5V)");
            Serial.println("Press Enter to continue...");
            while (!Serial.available()) delay(100);
            while (Serial.available()) Serial.read();
            break;
            
        case '4':
            Serial.println("Setting TX LOW...");
            digitalWrite(TX_PIN, LOW);
            Serial.println("Done. Measure B1 pin on TXS0108E (should be ~0V)");
            Serial.println("Press Enter to continue...");
            while (!Serial.available()) delay(100);
            while (Serial.available()) Serial.read();
            break;
            
        case '5':
            Serial.println("Toggling DE/RE slowly (measure B3)...");
            for (int i = 0; i < 3; i++) {
                digitalWrite(DE_RE_PIN, HIGH);
                Serial.println("DE/RE HIGH - Press Enter for next step...");
                while (!Serial.available()) delay(100);
                while (Serial.available()) Serial.read();
                
                digitalWrite(DE_RE_PIN, LOW);
                Serial.println("DE/RE LOW - Press Enter for next step...");
                while (!Serial.available()) delay(100);
                while (Serial.available()) Serial.read();
            }
            break;
            
        case '6':
            Serial.println("Toggling TX slowly (measure B1)...");
            for (int i = 0; i < 3; i++) {
                digitalWrite(TX_PIN, HIGH);
                Serial.println("TX HIGH - Press Enter for next step...");
                while (!Serial.available()) delay(100);
                while (Serial.available()) Serial.read();
                
                digitalWrite(TX_PIN, LOW);
                Serial.println("TX LOW - Press Enter for next step...");
                while (!Serial.available()) delay(100);
                while (Serial.available()) Serial.read();
            }
            break;
            
        case 'r':
            Serial.printf("Pin states - DE/RE: %d, TX: %d, RX: %d\n", 
                digitalRead(DE_RE_PIN), digitalRead(TX_PIN), digitalRead(RX_PIN));
            delay(2000);
            break;
            
        default:
            Serial.println("Unknown command");
            delay(2000);
            break;
    }
}
