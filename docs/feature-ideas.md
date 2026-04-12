# Feature Ideas

> Ideer och tankar kring framtida utveckling. Inget beslut taget - detta ar en samling koncept att utvardera.

## Produktions-PCB

Ersatta devkits (ESP32-DevKitC, RS485 5 Click, TXS0108E breakout) med ett integrerat kretskort.

### Komponenter att integrera
- **ESP32-WROOM-32E** - direkt pa PCB (ersatter devkit)
- **MAX485ESA+T** - direkt pa PCB (ersatter Click-board)
- **TXS0108E** - direkt pa PCB (ersatter breakout)
- USB-UART (CP2102/CH340) for programmering
- Spannningsregulatorer (AMS1117-3.3 + AMS1117-5.0)
- Skruvplintar for RS485, pumpar, nivasensorer

### Kanda problem att fixa i schemat
- [x] TX/RX-korsning - Click-kortets TX/RX-labels ar ur MAX485-perspektiv, inte ESP32 (fixat i mjukvara 2026-04-12)
- [ ] OE pa TXS0108E - felkopplad i nuvarande PCB-layout (byglad externt)
- [ ] DE/RE pa RS485 Click - separata pinnar, maste bridgas (byglad externt)

### Fordelar
- Mindre fysisk storlek
- Hogre tillforlitlighet (inga losa kontakter/pin-headers)
- Billigare i produktion
- Enklare montering i IP65-kapslingar

---

## Multi-zon bevattning

Utoka fran en enda bevattningspunkt (vaxthuset) till flera zoner pa tomten.

### Koncept
- Varje zon har en **lokal cistern**, **pump**, **jordsensor** och **ESP32**
- En **gateway/huvudenhet** samlar data och koordinerar pafyllning
- Varje nod ar **autonom** - vattnar sjalv aven om kommunikationen gar ner
- Central pafyllnadspump/ventiler fyller lokala cisterner vid behov

### Arkitekturskiss
```
[Huvudenhet / Gateway]
  - ESP32 med WiFi -> hem-nat/dashboard
  - Tradlos kommunikation -> alla bevattningsnoder
  - Styr central pafyllnadspump/ventiler

[Bevattningsnod 1..N]  (lokal cistern + pump + sensorer)
  - ESP32 med tradlos kommunikation
  - Lokal jordsensor (Modbus RS485)
  - Lokal pump + cistern med nivasensorer
  - Autonom bevattning
  - Rapporterar status till gateway
  - Begar pafyllning nar cisternen ar lag

[Pafyllnadssystem]
  - Kan vara i gateway eller separat enhet
  - Styr ventiler/pump for att fylla respektive cistern
```

### Kommunikation - LoRa vs ESP-NOW

| | LoRa | ESP-NOW |
|---|---|---|
| **Rackvidd** | 1-10 km (oppen mark) | ~200m (oppen mark) |
| **Datarate** | Lag (0.3-50 kbps) | Hog (1 Mbps) |
| **Latens** | Hog (sekunder) | Lag (ms) |
| **Extra hardvara** | Ja (LoRa-modul) | Nej, finns i ESP32 |
| **Stromforbrukning** | Mycket lag | Lag |
| **Max peers** | Nastan obegransat | 250 |
| **Kostnad per nod** | +50-100 SEK (LoRa-modul) | 0 SEK (inbyggt) |

**Preliminar bedomning:** ESP-NOW racker troligtvis for en tomt (<200m avstand). LoRa ar relevant om avstand/terraeng/byggnader kraver det.

---

*Skapad: 2026-04-12*
*Status: Idestadiet - inget beslut taget*
