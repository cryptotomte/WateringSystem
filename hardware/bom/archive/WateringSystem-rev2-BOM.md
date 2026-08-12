# WateringSystem rev2 – BOM

Bill of Materials för custom-PCB Fas 0 enligt plan i `/Users/pw/.claude/plans/om-vi-skulle-ta-parsed-giraffe.md`.

Mål: ersätta ESP32-devkort + MIKROE RS485 5 Click + TXS0108E breakout + extern LDO med ett eget kort. Allt SMD utom terminalblock och kontakter.

> **BESLUT 2026-06-10 — en pumpkanal.** Rev2 är *nod*-hårdvaran i det planerade
> multi-zon-nätet (`docs/feature-ideas.md`): reservoarpåfyllning sköts av en
> framtida central reservoar-enhet (en pump + solenoidventiler), så noden har
> EN pumpkanal (bevattningspumpen). Antal för Q2/D1/D2/R1/R8/R9/J3 och INA226
> är halverade mot tidigare utkast. Nivåsensorerna (J5, Q1, R6/R7) behålls —
> de driver påfyllnadsbegäran och lokala fail-safes. Tills centralenheten
> finns fylls växthusnodens reservoar manuellt.

## Aktiva komponenter

| Ref | Qty | Komponent | Paket | MPN | Roll |
|---|---|---|---|---|---|
| U1 | 1 | ESP32-WROOM-32E | Module | ESP32-WROOM-32E-N4 | MCU, samma som idag |
| U2 | 1 | CP2102N | QFN-28 | CP2102N-A02-GQFN28R | USB-UART för programmering |
| U3 | 1 | THVD1426 | SOT-8 (5×3) | THVD1426DRLR | RS485-transceiver, auto-direction, 3–5.5V |
| U4 | 1 | MP2307 | SOIC-8-EP | MP2307DN | 12V→3.3V buck, 3A |
| U5 | 1 | INA226 | MSOP-10 | INA226AIDGSR | Current/voltage/power monitor I2C |
| U6 | 1 | BME280 | LGA-8 | BME280 | Miljösensor, samma som idag |

## MOSFETs & dioder

| Ref | Qty | Komponent | Paket | MPN | Roll |
|---|---|---|---|---|---|
| Q1 | 2 | 2N7002 | SOT-23 | 2N7002LT1G | Level-shift XKC-Y26 OUT till 3.3V |
| Q2 | 1 | AOD514 | DPAK | AOD514 | Pump-switch (bevattningspump), 75A logic-level |
| Q3 | 1 | AO3401 | SOT-23 | AO3401A | Reverse-polarity-skydd 12V-input |
| D1 | 1 | SS54 | SMA | SS54 | Flyback över pump |
| D2 | 1 | SMBJ16CA | SMB | SMBJ16CA | TVS över AOD514 drain-source |
| D3 | 1 | SMBJ15CA | SMB | SMBJ15CA | TVS på 12V-rail |
| D4 | 1 | SM712 | SOT-23 | SM712 | RS485 A/B TVS-array |

## Passiva

### Strömmätning (shunt)
| Ref | Qty | Värde | Paket | MPN | Roll |
|---|---|---|---|---|---|
| R1 | 1 | 2 mΩ 1W 1% | 2512 | KRL3216E-C-R002-F-T1 | Pump-shunt, i serie med pump (+) |

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
| R8 | 1 | 10 kΩ | 0603 | RC0603FR-0710KL | AOD514 gate-pulldown |
| R9 | 1 | 100 Ω | 0603 | RC0603FR-07100RL | AOD514 gate-resistor |

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
| C3-C4 | 4 | 100 nF 16V | 0402 | GRM155R61C104KA88D | Bypass per IC (en nära VCC på varje) |
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
| J3 | 1 | Weidmüller 2-pin terminal | 5.08 mm pitch | (välj modell) | Pump-output (bevattningspump) |
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
- THVD1426DRLR (SOT-8, C5215922) vald — andrakälla Mouser 595-THVD1426DRLR. Se hardware/rev2/design-notes/02-rs485.md för bias/sourcing-detaljer.
- MP2307 ja
- Vanliga passiva (0603, 0805) alla standard

Om JLCPCB EMS används: konvertera BOM till deras format (KiCad → Tools → Generate BOM med deras script). Pris exkl. moms ~$80 för 5 monterade kort (PCB + assembly + components).

## Anteckningar

- **Varför THVD1426 istället för MAX13487E (rättat juni 2026)**: MAX13487E kräver 5V-matning (4.75–5.25V) men rev2 har bara en 3.3V-rail (MP2307) – dess RO-utgång hade dessutom drivit 5V-logik in i ESP32:ans RX. TI THVD1426 ger samma auto-direction-funktion (via D-pinnen, ingen DE behövs) men drivs 3–5.5V och har ±12kV IEC ESD inbyggt. Auto-direction förutsätter bias-resistorerna R4 (pullup A→3.3V) och R5 (pulldown B→GND) så bussen vilar i definierat idle-läge – dessa finns redan i BOM. Verifiera tHOLD/driver-release i databladet mot 9600 baud (104 µs bittid) vid schemaritning.
- **INA226 A0/A1**: INA226 har 4 möjliga I2C-adresser via A0/A1-pinnarna. Med en enda INA226 räcker default-strappning (0x40) — krockar inte med BME280 (0x76/0x77).
- **AOD514 thermal**: vid 15A kontinuerligt blir effektförlusten 1.3W. Lägg minst 2 cm² koppar-pour under exposed pad + 6 thermal vias till bottenlager för värmespridning.
- **Buck-layout**: håll input-cap (C1), MP2307, induktor (L1) och output-cap (C2) i en kompakt loop. Dålig layout = EMI som stör RS485.
- **Shunt-placering (R1)**: high-side (mellan 12V-rail och pump+) ger bäst INA226-mätning eftersom common-mode-spänningen då är 12V (within INA226's 0–36V range).
- **En pumpkanal (beslut 2026-06-10)**: ingen reservoarpump på noden — påfyllning sköts centralt i multi-zon-arkitekturen (se beslutsruta överst och `docs/feature-ideas.md`). CSV-filen för Mouser/JLCPCB behöver uppdateras med samma antalsändringar innan beställning.
- **USB-C vs USB-A**: USB-C valt för framtidssäkring. CC1/CC2 pulldowns (R10) gör att kortet identifierar sig som USB device till sources.

## Ändringslogg — multi-nod-beslut (2026-06-11)

Beslut från H1–H4-rundan (batteridrift, ESP-NOW-avstånd OK, extern antenn, expansion). Påverkar schemablock men få befintliga BOM-rader; nya komponenter får slutgiltiga värden i respektive blocks design-notes (`hardware/rev2/design-notes/`).

1. **Modul: ESP32-WROOM-32UE som default** (U.FL för extern antenn på IP66/68-lådan). Samma footprint som -32E; -32E kvarstår som bestyckningsalternativ. U.FL→SMA-bulkhead-pigtail per nod tillkommer (mekanik-BOM).
2. **Kraftkälla: 12,8V LiFePO4-batteri + solpanel via extern laddregulator.** Kortet matas från batteripolerna via inline-säkring — INTE från regulatorns load-utgång. Ingången ska tåla laddspänning ~14,6V (MP2307 OK, marginal till 23V). Polskydd + TVS på J2.
3. **Brytbar 12V-rail för sensor/RS485** via high-side switch (P-MOSFET + 2N7002-driver, komponentval i block 4/RS485): NPK-sensorn drar ~0,5 W kontinuerligt = ~1 Ah/dygn om den står på; måste kunna slås av mellan mätningar för batteridrift. GPIO-styrd, default AV (pull-down).
4. **Batterispänningsmätning**: resistiv delare (högohmig, ≥100kΩ totalt för låg viloström) + RC-filter till ESP32-ADC. Ger telemetri + mjuk UVLO i firmware (BMS = hård gräns, regulator = mellangräns).
5. **Expansionsheader J7**: 8-pin 0.1" med 3V3/GND + SPI eller UART + 1–2 GPIO — framtida radio (LoRa-hedge), display eller sensorer utan kortrevision. Pinnval låses vid SYNK 1.
6. **Driftprofil**: hårdvaran ska stödja både alltid-på (nod 1/växthuset hostar web-UI tills master finns) och deep-sleep-profil (senare, ren firmwarefråga). Inga alltid-på-förbrukare på kortet utöver buck + MCU.
7. **Valbar INA226-position för solpanelstelemetri (DNP som default)**: pass-through-mätloop på panelens plusledare (panel → J8 → shunt → J8 → laddregulator PV+), egen I2C-adress 0x41 (A0=VS). Ger panelproduktion i API:t utan egen laddregulator. Förutsätter common-negative laddregulator [VERIFY vid regulatorköp]. Bestyckas per nod vid behov.

Rekommenderad extern materiel per nod (utanför kort-BOM): 12,8V 7,2Ah LiFePO4 m. BMS (t.ex. V-TAC), stel 20W mono glaspanel, laddregulator med äkta LiFePO4-profil (Epever/Renogy-klass; PWM-paketregulatorer från AliExpress avråds — blyprofil).

## Källfiler

- CSV-version för Mouser/Digikey-import: `WateringSystem-rev2-BOM.csv` (samma katalog)
- KiCad-projekt (nuvarande rev1): `hardware/WateringSystem.kicad_pro` – ny rev2-skiva rekommenderas (se plan, sektion 0.8)
