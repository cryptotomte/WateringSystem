# WateringSystem rev2 – BOM

Bill of Materials för custom-PCB Fas 0 enligt plan i `/Users/pw/.claude/plans/om-vi-skulle-ta-parsed-giraffe.md`.

Mål: ersätta ESP32-devkort + MIKROE RS485 5 Click + TXS0108E breakout + extern LDO med ett eget kort. Allt SMD utom terminalblock och kontakter. Körs med oförändrad Arduino-firmware initialt.

## Aktiva komponenter

| Ref | Qty | Komponent | Paket | MPN | Roll |
|---|---|---|---|---|---|
| U1 | 1 | ESP32-WROOM-32E | Module | ESP32-WROOM-32E-N4 | MCU, samma som idag |
| U2 | 1 | CP2102N | QFN-28 | CP2102N-A02-GQFN28R | USB-UART för programmering |
| U3 | 1 | THVD1426 | SOIC-8 | THVD1426DR | RS485-transceiver, auto-direction, 3–5.5V |
| U4 | 1 | MP2307 | SOIC-8-EP | MP2307DN | 12V→3.3V buck, 3A |
| U5 | 2 | INA226 | MSOP-10 | INA226AIDGSR | Current/voltage/power monitor I2C |
| U6 | 1 | BME280 | LGA-8 | BME280 | Miljösensor, samma som idag |

## MOSFETs & dioder

| Ref | Qty | Komponent | Paket | MPN | Roll |
|---|---|---|---|---|---|
| Q1 | 2 | 2N7002 | SOT-23 | 2N7002LT1G | Level-shift XKC-Y26 OUT till 3.3V |
| Q2 | 2 | AOD514 | DPAK | AOD514 | Pump-switch, 75A logic-level |
| Q3 | 1 | AO3401 | SOT-23 | AO3401A | Reverse-polarity-skydd 12V-input |
| D1 | 2 | SS54 | SMA | SS54 | Flyback över pump |
| D2 | 2 | SMBJ16CA | SMB | SMBJ16CA | TVS över AOD514 drain-source |
| D3 | 1 | SMBJ15CA | SMB | SMBJ15CA | TVS på 12V-rail |
| D4 | 1 | SM712 | SOT-23 | SM712 | RS485 A/B TVS-array |

## Passiva

### Strömmätning (shunt)
| Ref | Qty | Värde | Paket | MPN | Roll |
|---|---|---|---|---|---|
| R1, R2 | 2 | 2 mΩ 1W 1% | 2512 | KRL3216E-C-R002-F-T1 | Pump-shunt, i serie med pump (+) |

### RS485-bus
| Ref | Qty | Värde | Paket | MPN | Roll |
|---|---|---|---|---|---|
| R3 | 1 | 120 Ω | 0603 | RC0603FR-07120RL | RS485-terminering A↔B |
| R4 | 1 | 680 Ω | 0603 | RC0603FR-07680RL | Bias-pullup A→3.3V |
| R5 | 1 | 680 Ω | 0603 | RC0603FR-07680RL | Bias-pulldown B→GND |

### Sensor- och MOSFET-resistorer
| Ref | Qty | Värde | Paket | MPN | Roll |
|---|---|---|---|---|---|
| R6 | 2 | 10 kΩ | 0603 | RC0603FR-0710KL | XKC-Y26 OUT pullup till 12V |
| R7 | 2 | 10 kΩ | 0603 | RC0603FR-0710KL | 2N7002 drain pullup till 3.3V |
| R8 | 2 | 10 kΩ | 0603 | RC0603FR-0710KL | AOD514 gate-pulldown |
| R9 | 2 | 100 Ω | 0603 | RC0603FR-07100RL | AOD514 gate-resistor |

### USB-C och ESP32 boot
| Ref | Qty | Värde | Paket | MPN | Roll |
|---|---|---|---|---|---|
| R10 | 2 | 5.1 kΩ | 0603 | RC0603FR-075K1L | USB-C CC1/CC2 pulldown |
| R11 | 2 | 10 kΩ | 0603 | RC0603FR-0710KL | EN och GPIO0 pullups |

### Kondensatorer
| Ref | Qty | Värde | Paket | MPN | Roll |
|---|---|---|---|---|---|
| C1 | 4 | 10 µF 25V | 0805 | GRM21BR61E106KA73L | Buck input + power-rail-bypass |
| C2 | 2 | 22 µF 10V | 0805 | GRM21BR61A226KE51L | Buck output cap (3.3V) |
| C3-C4 | 5 | 100 nF 16V | 0402 | GRM155R61C104KA88D | Bypass per IC (en nära VCC på varje) |
| C5 | 1 | 470 µF 25V | SMD elec | UWT1E471MNL1GS | Bulk input cap för pumpstart-transienter |

### Buck-induktor
| Ref | Qty | Värde | Paket | MPN | Roll |
|---|---|---|---|---|---|
| L1 | 1 | 22 µH 3A | SMD shielded | XAL5030-223MEC | För MP2307 |

## Skydd

| Ref | Qty | Komponent | Paket | MPN | Roll |
|---|---|---|---|---|---|
| F1 | 1 | Polyfuse 1A | 1812 | 0ZCJ0100AF2C | Resettable fuse på 12V-input |

## LEDs

| Ref | Qty | Färg | Paket | MPN | Roll |
|---|---|---|---|---|---|
| LED1 | 1 | Grön | 0603 | LTST-C190KGKT | Power-indikator (3.3V via 1 kΩ) |
| LED2 | 1 | Röd | 0603 | LTST-C190KRKT | Status-LED (GPIO2) |

## Mekanik & kontakter

| Ref | Qty | Komponent | Paket | MPN | Roll |
|---|---|---|---|---|---|
| SW1 | 1 | Tactile switch | SMD 6×6mm | PTS636SK50SMTR LFS | BOOT-knapp |
| SW2 | 1 | Tactile switch | SMD 6×6mm | PTS636SK50SMTR LFS | RESET-knapp |
| J1 | 1 | USB-C receptacle | 16-pin SMD | USB4105-GF-A | Programmering / 5V-alternativ |
| J2 | 1 | Weidmüller 2-pin terminal | 5.08 mm pitch | (välj modell) | 12V-input |
| J3 | 2 | Weidmüller 2-pin terminal | 5.08 mm pitch | (välj modell) | Pump-output |
| J4 | 1 | JST XH 4-pin header | XH-4P | B4B-XH-A(LF)(SN) | Soil-sensor RS485 (A/B/12V/GND) |
| J5 | 2 | JST XH 4-pin header | XH-4P | B4B-XH-A(LF)(SN) | XKC-Y26 (VCC/OUT/GND/MODE) |
| J6 | 1 | 6-pin 0.1" header | THT eller TC2030 | (välj) | JTAG för ESP-IDF debug |

## Sammanfattning kostnad (uppskattning, 1st)

| Kategori | Kostnad |
|---|---|
| Aktiva IC | ~$18 |
| MOSFETs & dioder | ~$3.5 |
| Passiva | ~$5 |
| Mekanik & kontakter | ~$5 |
| **Totalt komponenter per kort** | **~$30–35** |
| PCB (JLCPCB 4-lager, 5st) | ~$20 ($4/st) |
| **Totalt per kort** | **~$35–40** |

Reduceras vid större volymer (priser ovan är 1st-priser från Mouser/Digikey).

## Inköpsstrategi

**Alt A — Mouser samlad order**
Importera CSV-filen `WateringSystem-rev2-BOM.csv` i Mouser BOM Tool (Tools → BOM Tool → Upload). Mouser matchar MPN automatiskt och visar lagerstatus. Frakt till Sverige ~$20.

**Alt B — Digikey samlad order**
Liknande process via Digikey "myLists". Båda har god lagerföring av samtliga komponenter ovan.

**Alt C — JLCPCB EMS (assembly service)**
JLCPCB har "Standard Parts" och "Extended Parts" library. Av komponenterna ovan finns följande som standard parts (förförda i deras maskin, billigare):
- ESP32-WROOM-32E ja
- INA226 ja
- BME280 ja (om LCSC har modulen)
- AOD514 finns ej alltid → välj alternativ från LCSC: t.ex. SiSF20DN-T1-GE3
- THVD1426 blir extended part. OBS: LCSC lagerför SOT-varianten THVD1426DRLR (C5215922); SOIC-8-varianten THVD1426DR finns hos Mouser/Digikey. Välj paket innan footprint låses.
- MP2307 ja
- Vanliga passiva (0603, 0805) alla standard

Om JLCPCB EMS används: konvertera BOM till deras format (KiCad → Tools → Generate BOM med deras script). Pris exkl. moms ~$80 för 5 monterade kort (PCB + assembly + components).

## Anteckningar

- **Varför THVD1426 istället för MAX13487E (rättat juni 2026)**: MAX13487E kräver 5V-matning (4.75–5.25V) men rev2 har bara en 3.3V-rail (MP2307) – dess RO-utgång hade dessutom drivit 5V-logik in i ESP32:ans RX. TI THVD1426 ger samma auto-direction-funktion (via D-pinnen, ingen DE behövs) men drivs 3–5.5V och har ±12kV IEC ESD inbyggt. Auto-direction förutsätter bias-resistorerna R4 (pullup A→3.3V) och R5 (pulldown B→GND) så bussen vilar i definierat idle-läge – dessa finns redan i BOM. Verifiera tHOLD/driver-release i databladet mot 9600 baud (104 µs bittid) vid schemaritning.
- **INA226 A0/A1**: INA226 har 4 möjliga I2C-adresser via A0/A1-pinnarna. Konfigurera olika på de två chippen så de inte krockar med varandra eller med BME280 (0x76/0x77).
- **AOD514 thermal**: vid 15A kontinuerligt blir effektförlusten 1.3W. Lägg minst 2 cm² koppar-pour under exposed pad + 6 thermal vias till bottenlager för värmespridning.
- **Buck-layout**: håll input-cap (C1), MP2307, induktor (L1) och output-cap (C2) i en kompakt loop. Dålig layout = EMI som stör RS485.
- **Shunt-placering (R1, R2)**: high-side (mellan 12V-rail och pump+) ger bäst INA226-mätning eftersom common-mode-spänningen då är 12V (within INA226's 0–36V range).
- **Reservoir-pump-currentsense**: båda pumparna får INA226 + shunt. Reservoarpumpen drar typiskt mindre, men diagnostik är värdefullt även där.
- **USB-C vs USB-A**: USB-C valt för framtidssäkring. CC1/CC2 pulldowns (R10) gör att kortet identifierar sig som USB device till sources.

## Källfiler

- CSV-version för Mouser/Digikey-import: `WateringSystem-rev2-BOM.csv` (samma katalog)
- KiCad-projekt (nuvarande rev1): `hardware/WateringSystem.kicad_pro` – ny rev2-skiva rekommenderas (se plan, sektion 0.8)
