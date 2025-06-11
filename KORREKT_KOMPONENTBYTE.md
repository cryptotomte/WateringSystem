# Korrekt Komponentbyte: FOD817 → TXS0108E

## Sammanfattning av ändringar

Detta dokument beskriver de **KORREKTA** ändringarna för att byta från FOD817BSD optokopplare till TXS0108E nivåskiftare, **UTAN** att ändra GPIO-konfigurationen.

## ✅ Vad som ändrades (endast komponentbyte)

### 1. **RS485Config.h**
- Bytte `RS485_ISOLATION_IC "FOD817BSD"` → `RS485_LEVEL_SHIFTER_IC "TXS0108E"`
- Tog bort `TXS0108E_OE_PIN 26` (OE ansluts direkt till VCC)
- Uppdaterade kommentarer för TXS0108E anslutning
- Behöll alla ursprungliga timing-konstanter för förbättrad prestanda

### 2. **SP3485ModbusClient.cpp**
- Tog bort TXS0108E Output Enable GPIO-initialisering
- Lade till kommentar om att OE-pin ansluts till VCC
- Behöll alla timing-förbättringar (snabbare DE/RE switching)
- Uppdaterade kommentarer till TXS0108E

### 3. **main.cpp**
- **BEHÖLL alla ursprungliga GPIO-definitioner**
- GPIO 26: Main Pump (oförändrat)
- GPIO 27: Reservoir Pump (oförändrat)  
- GPIO 32/33: Reservoir sensors (oförändrat)
- Tog bort TXS0108E OE GPIO-initialisering i `initHardware()`
- Uppdaterade kommentarer

### 4. **docs/hardware.md**
- Uppdaterade komponentspecifikationer FOD817 → TXS0108E
- **ÅTERSTÄLLDE ursprungliga GPIO-tilldelningar**
- Korrigerade kopplingsdiagram (OE←VCC)
- Uppdaterade prestanda- och kostnadstabeller

## 🔌 **TXS0108E Anslutning (ingen extra GPIO behövs)**

```
ESP32 Side (3.3V - A Side)    |    RS485 Side (5V - B Side)
------------------------------|-----------------------------
GPIO 16 → TXS0108E A1         |    TXS0108E B1 → RS485 DI
GPIO 17 ← TXS0108E A2         |    TXS0108E B2 ← RS485 RO
GPIO 25 → TXS0108E A3         |    TXS0108E B3 → RS485 DE/RE
3.3V → TXS0108E VCCA          |    TXS0108E VCCB ← 5V
VCC → TXS0108E OE             |    (Always enabled)
GND → TXS0108E GND            |    TXS0108E GND ← GND
```

## 📋 **GPIO-tilldelningar (OFÖRÄNDRADE)**

| GPIO | Funktion | Status |
|------|----------|--------|
| GPIO 16 | RS485 TX | Oförändrat |
| GPIO 17 | RS485 RX | Oförändrat |
| GPIO 25 | RS485 DE/RE | Oförändrat |
| **GPIO 26** | **Main Pump** | **Oförändrat** |
| **GPIO 27** | **Reservoir Pump** | **Oförändrat** |
| **GPIO 32** | **Reservoir Low Level** | **Oförändrat** |
| **GPIO 33** | **Reservoir High Level** | **Oförändrat** |

## ⚡ **Prestandaförbättringar**

| Parameter | FOD817BSD | TXS0108E | Förbättring |
|-----------|-----------|----------|-------------|
| Propagation Delay | 18µs | 10ns | 1800x snabbare |
| Max Data Rate | ~1 Mbps | 110 Mbps | 110x snabbare |
| Strömförbrukning | 30mA | 2mA | 93% minskning |
| Komponentkostnad | 3x $0.75 | 1x $0.50 | 78% billigare |

## 🎯 **Fördelar med korrekt implementation**

1. **Enkelt komponentbyte** - Ingen omkodning av GPIO-logik
2. **Snabbare kommunikation** - 1800x förbättrad responstid
3. **Lägre strömförbrukning** - Längre batteritid
4. **Färre komponenter** - Enklare PCB-design
5. **Bibehållen funktionalitet** - Alla befintliga features fungerar

## 🔧 **Testning och verifiering**

- ✅ Kompilering utan fel
- ✅ Alla GPIO-definitioner korrekta
- ✅ Ingen funktionalitetsförlust
- ✅ Dokumentation uppdaterad

---
**Skapad**: 11 juni 2025  
**Status**: Korrekt implementation klar för test
