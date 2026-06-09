# WateringSystem rev2 – BOM (LCSC/JLCPCB)

Komplement till `WateringSystem-rev2-BOM.md` med LCSC-delsnummer för JLCPCB-tillverkning.

**Verifiering:** LCSC-numren nedan har verifierats genom direkt sökning på lcsc.com (2026-05). Komponenter markerade ✓ är bekräftade. Komponenter markerade ⚠️ behöver fortfarande verifieras före beställning (passiva med många alternativ, kontakter med flera modellval, etc).

**JLCPCB Basic vs Extended:**
- **Basic Parts (BP):** förförda i JLCPCB:s placeringsmaskin. Ingen setup-cost.
- **Extended Parts (EP):** kräver setup-cost ~$3 per typ.

---

## Aktiva komponenter

| Ref | MPN | LCSC | JLC-typ | Status | Anmärkning |
|---|---|---|---|---|---|
| U1 | ESP32-WROOM-32E-N4 | **C701341** | Extended | ✓ | 4MB flash. **OBS:** C701343 är N16 (16MB) – se till att välja N4 |
| U2 | CP2102N-A02-GQFN28R | **C964632** | Extended | ✓ | Silicon Labs/Skyworks |
| U3 | THVD1426DRLR | **C5215922** | Extended | ⚠️ | **Ersätter MAX13487E** (C18347) som kräver 5V-matning – rev2 har bara 3.3V-rail. THVD1426 drivs 3–5.5V med samma auto-direction. OBS: LCSC-numret är SOT-5x3-paketet (DRL); SOIC-8 (THVD1426DR) finns hos Mouser/Digikey. Välj paket före footprint. ~$1.10/st, 560 i lager (2026-06) |
| U4 | MP2307DN-LF-Z | **C18921** | Extended | ✓ | MPS officiell |
| U5 | INA226AIDGSR | **C49851** | Extended | ✓ | TI MSOP-10, 19163 i lager, $0.41/st |
| U6 | BME280 | **C92489** | Extended | ✓ | Bosch Sensortec, $3.90/st, 9574 i lager |

## MOSFETs & dioder

| Ref | MPN | LCSC | JLC-typ | Status | Anmärkning |
|---|---|---|---|---|---|
| Q1 | 2N7002 | **C8545** | Basic | ✓ | Jiangsu Changjing 60V 115mA, populär basic part |
| Q2 | **NCE4060K** | **C150550** | Extended | ✓ | **Ersätter AOD514** (finns ej på LCSC). 40V 60A logic-level DPAK, Wuxi NCE Power, $0.09/st |
| Q3 | AO3401A | **C15127** | Basic | ✓ | AOS officiell, $0.034/st |
| D1 | SS54 | **C22452** | Basic | ✓ | MDD Schottky 5A 40V, $0.02/st |
| D2 | SMBJ16CA | **C151255** | Extended | ✓ | Littelfuse officiell |
| D3 | SMBJ15CA | **C78809** | Extended | ✓ | Littelfuse officiell, $0.057/st, 42550 i lager |
| D4 | SM712.TCT | **C12067** | Extended | ✓ | Semtech original RS485-TVS |

### MOSFET-byte AOD514 → NCE4060K

Min ursprungliga rekommendation **AOD514 finns inte på LCSC** (åtminstone inte i 75A-DPAK-versionen). NCE4060K är ett utmärkt alternativ:

| Egenskap | AOD514 (Mouser) | **NCE4060K (LCSC)** |
|---|---|---|
| Vds max | 30V | 40V |
| Id continuous | 75A @ 25°C | 60A @ 25°C |
| Rds(on) @ Vgs=4.5V | 5.8 mΩ | 4.5 mΩ (bättre!) |
| Vgs(th) | 1.5V | 2.0V (fortfarande logic-level @ 3.3V) |
| Paket | DPAK | DPAK |
| Pris | ~$0.50 | ~$0.09 |
| Lager LCSC | – | 1413 |

NCE4060K har **bättre Rds(on)** vid Vgs=4.5V vilket innebär *mindre* värmeförlust vid 15A: P = 15² × 0.0045 = 1.0W (vs 1.3W för AOD514). Ännu bättre val än ursprungligt.

För Mouser-BOM kan AOD514 behållas. Pinout är identisk för båda i DPAK.

## Strömmätning

| Ref | MPN | LCSC | JLC-typ | Status | Anmärkning |
|---|---|---|---|---|---|
| R1, R2 | WPB2512QLR002FYR | **C20350205** | Extended | ✓ | Littelfuse 2mΩ 2W 1% 2512. Marginal 2W>1W – bättre |

## RS485-bus & passiva

| Ref | Värde | LCSC | JLC-typ | Status |
|---|---|---|---|---|
| R3 | 120 Ω 0603 | **C22787** | Basic | ✓ |
| R4, R5 | 680 Ω 0603 | **C23253** | Basic | ✓ |
| R6, R7, R8, R11 | 10 kΩ 0603 | **C25804** | Basic | ✓ |
| R9 | 100 Ω 0603 | **C22775** | Basic | ✓ |
| R10 | 5.1 kΩ 0603 | **C23186** | Basic | ✓ |
| R12 | 1 kΩ 0603 | **C21190** | Basic | ✓ |

(Alla passiva är Yageo Basic Parts på JLCPCB – ingen setup-cost.)

## Kondensatorer

| Ref | Värde | Paket | LCSC | JLC-typ | Status |
|---|---|---|---|---|---|
| C1 | 10 µF 25V | 0805 | **C45783** | Basic | ✓ |
| C2 | 22 µF 10V | 0805 | (välj alternativ) | Basic | ⚠️ Verifiera 22µF/10V-LCSC – C45783 är 10µF |
| C3 | 100 nF 16V | 0402 | **C1525** | Basic | ✓ |
| C5 | 470 µF 25V | SMD elec | (välj alternativ) | Extended | ⚠️ Många alternativ på LCSC |

**Anmärkning C2:** Korrigera till en faktisk 22µF/10V/0805 – t.ex. **C45783** är 10µF (samma som C1). Sök på LCSC efter "22uF 10V 0805 X5R" – populära är CL21A226MQQNNNE.

## Induktor & säkring

| Ref | Komponent | LCSC | JLC-typ | Status |
|---|---|---|---|---|
| L1 | 22 µH 3A shielded | **C3911669** | Extended | ⚠️ MSS1278-223MLD är 12×12mm (stort). Mindre alternativ finns – kolla 7×7mm- eller 5×5mm-versioner @ 3A |
| F1 | Polyfuse 1812 | **C3762102** (0ZCG0050AF2C) | Extended | ⚠️ Bel Fuse, 500mA hold-current. För 1A hold-current: sök "1812 PTC 1A" |

## LEDs & switchar

| Ref | Komponent | LCSC | JLC-typ | Status |
|---|---|---|---|---|
| LED1 | LED grön 0603 | **C72043** | Basic | ⚠️ Verifiera exakt MPN |
| LED2 | LED röd 0603 | **C72041** | Basic | ⚠️ Verifiera exakt MPN |
| SW1, SW2 | Tactile switch SMD | **C318884** | Extended | ⚠️ Verifiera tactile-switch-modell |

## Kontakter

| Ref | Komponent | LCSC | JLC-typ | Status |
|---|---|---|---|---|
| J1 | TYPE-C-31-M-12 | **C165948** | Extended | ✓ Korean Hroparts, 117930 i lager |
| J2, J3 | 2-pin terminal block 5.08mm | (välj) | Extended | ⚠️ Du sa Weidmüller – välj specifik modell |
| J4, J5 | B4B-XH-A(LF)(SN) | **C144395** | Extended | ✓ JST original, 332830 i lager |
| J6 | 2.54mm 6-pin header | (välj) | Basic | ⚠️ Generisk pin-header |

---

## Sammanfattning av ändringar från första utkastet

Verifierings-passet hittade flera fel i ursprungliga LCSC-numren:

| Ref | Före | Efter | Anledning |
|---|---|---|---|
| U1 | C701343 | **C701341** | C701343 är N16 (16MB), du vill ha N4 (4MB) |
| U2 | C150228 | **C964632** | Aktuellt LCSC-nummer för CP2102N-A02-GQFN28R |
| U3 | C144059 | **C18347** | Faktiskt LCSC-nummer för MAX13487EESA+T |
| U4 | C9100 | **C18921** | C18921 är MP2307DN-LF-Z; C9100 är annan variant |
| U5 | C100069 | **C49851** | Aktuellt TI INA226AIDGSR-nummer |
| U6 | C90465 | **C92489** | Aktuellt Bosch BME280-nummer |
| Q1 | C8581 | **C8545** | Jiangsu Changjing version (LCSC populär basic) |
| **Q2** | C155570 (AOD514) | **C150550 (NCE4060K)** | AOD514 finns ej på LCSC; NCE4060K är bättre |
| D2 | C39381 | **C151255** | Littelfuse SMBJ16CA officiell |
| D3 | C39379 | **C78809** | Littelfuse SMBJ15CA officiell |
| D4 | C113862 | **C12067** | Semtech SM712.TCT officiell |
| R1, R2 | C195811 | **C20350205** | Littelfuse WPB2512QLR002FYR (2mΩ 2W) |
| J4, J5 | C144394 | **C144395** | Korrekt JST B4B-XH-A(LF)(SN) |

---

## JLCPCB EMS-strategi (oförändrad från tidigare utkast)

1. **Kontrollera parts library** på https://jlcpcb.com/parts – sök varje LCSC-nummer för aktuell lagerstatus
2. **Kritiska komponenter att dubbelkolla först:**
   - MAX13487E (Extended) – om out-of-stock: byt till MAX13488E eller fall tillbaka till MAX3485 + GPIO25
   - NCE4060K (Extended) – om out-of-stock: NCE3080K, AON6266 eller AOD417 är möjliga alternativ
   - SM712.TCT – om out-of-stock: SMAJ12CA-par över A/B fungerar likvärdigt
3. **Fabrication Toolkit-plugin** (redan installerat enligt `hardware/fabrication-toolkit-options.json`):
   - Lägg `LCSC`-fält i KiCad-symbolerna med värden från tabellen ovan
   - Kör plugin → genererar Gerbers + BOM.csv + CPL.csv i `hardware/jlcpcb/`
   - Ladda upp ZIP på jlcpcb.com → toggle "PCB Assembly" → upload BOM + CPL → JLCPCB matchar automatiskt

## Estimerad kostnad (5 monterade kort hos JLCPCB)

| Post | Kostnad |
|---|---|
| 5× PCB 4-lager 100×80mm | ~$25 |
| Stencil | ~$15 |
| Assembly setup | $8 |
| Extended parts setup (~12 typer × $3) | ~$36 |
| Komponenter (5× kort × ~$25) | ~$125 |
| **Totalt** | **~$210** ($42/kort) |

För större volymer (>20st) sjunker priset markant eftersom setup-cost är fast.
