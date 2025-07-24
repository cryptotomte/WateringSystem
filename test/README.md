# RS485 Test Suite - WateringSystem

Detta är en samling testfiler för RS485 Modbus-kommunikation med jordsensorer, utvecklade under debugging-processen i juli 2025.

## Testfiler

### `test_level_shifter.cpp`
**Syfte:** Verifiera TXS0108E level shifter-funktionalitet
- Testar grundläggande GPIO-kommunikation genom level shifter
- Verifierar timing och signal-integritet
- Använd när du misstänker level shifter-problem

### `test_rs485_communication.cpp`
**Syfte:** Grundläggande RS485-kommunikationstest
- Enkel ping/pong-kommunikation
- Testar baud rate och timing
- Bra för initial debugging av RS485-anslutning

### `test_rs485_debug.cpp`
**Syfte:** Detaljerad debugging av RS485-protokoll
- Visar raw bytes som skickas/tas emot
- CRC-beräkning och validering
- Hex-output för protokollanalys

### `test_rs485_enhanced.cpp` ⭐ **REKOMMENDERAD**
**Syfte:** Komplett Modbus RTU-test med alla sensorfunktioner
- Läser alla register från jordsensorn (0x0000-0x000F)
- Interaktiv meny för specifika registerläsningar
- Auto-test sekvens för alla värden
- **Bevisad fungerande** - använd som referens för working implementation

## Historik och lärdomar

### Problemlösning Juli 2025
1. **Level shifter verification:** TXS0108E fungerar med 50µs delays
2. **Pin configuration fix:** GPIO 17=TX, GPIO 16=RX (var ombytta)
3. **Timing optimization:** Från 10µs till 50µs för pålitlig switching
4. **Protocol implementation:** Full Modbus RTU med CRC-validering

### Verifierade inställningar
```cpp
// Working configuration
#define TX_PIN 17
#define RX_PIN 16
#define DE_RE_PIN 25
#define RS485_DE_ASSERT_DELAY_US 50
#define RS485_DE_DEASSERT_DELAY_US 50
```

## Användning

### För att köra tester:
1. Ersätt innehållet i `src/main.cpp` med önskad testfil
2. Kompilera och ladda upp: `pio run -t upload`
3. Övervaka output: `pio device monitor -b 115200`

### För framtida debugging:
1. Börja med `test_rs485_enhanced.cpp` - den fungerar garanterat
2. Om den inte fungerar, kontrollera physical connections
3. Om den fungerar men huvudappen inte, jämför timing och pin-konfiguration
4. Använd `test_rs485_debug.cpp` för protokollanalys

### Return to main application:
```bash
git checkout src/main.cpp  # Återställ till huvudapplikation
pio run -t upload          # Ladda upp huvudappen
```

## Tekniska detaljer

### Modbus Register Map (NPK Soil Sensor)
| Register | Beskrivning | Enhet | Format |
|----------|-------------|-------|--------|
| 0x0000 | Jordfuktighet | % | Unsigned 16-bit |
| 0x0001 | Jordtemperatur | 0.1°C | Signed 16-bit |
| 0x0002 | pH-värde | 0.1 pH | Unsigned 16-bit |
| 0x0003 | EC (Electrical Conductivity) | µS/cm | Unsigned 16-bit |
| 0x0004 | Kväve (N) | mg/kg | Unsigned 16-bit |
| 0x0005 | Fosfor (P) | mg/kg | Unsigned 16-bit |
| 0x0006 | Kalium (K) | mg/kg | Unsigned 16-bit |

### Hardware Setup
- ESP32-WROOM-32E
- TXS0108E bidirectional level shifter (3.3V ↔ 5V)
- MikroElektronika RS485 5 Click (MIKROE-4156)
- NPK Modbus soil sensor (device address 0x01)

---
*Skapad: Juli 2025*  
*Status: Verifierad fungerande med jordsensor NPK*
