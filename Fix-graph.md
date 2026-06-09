# Graf-problem: Analys och Lösningar

**Datum:** 2025-01-02  
**Status:** Identifierad, ej åtgärdad  
**Analyserad av:** Claude Code via Playwright

## Problemsammanfattning

Webbgränssnittets graf i "Historical Data"-sektionen är tom och visar inga datapunkter, trots att realtidsdata fungerar korrekt.

## Identifierade Problem

### 1. Reading-dropdown populeras inte vid sidladdning
**Fil:** `data/script.js:865-886`  
**Problem:** `updateChartOptions()` körs aldrig automatiskt när sidan laddas.

**Symptom:**
- Sensor dropdown: "Environmental" (funkar)
- Reading dropdown: Tom vid sidladdning
- Time Range dropdown: "Last 24 hours" (funkar)

**Orsak:** Funktionen körs bara när användaren manuellt ändrar sensor-dropdown (rad 227 event listener).

**Lösning:** 
```javascript
// I initApp() funktionen, efter initChart():
updateChartOptions(); // Lägg till denna rad
```

### 2. Tom historisk data från servern
**Endpoint:** `/history?sensor=env&reading=temperature&range=24h`

**Aktuell respons:**
```json
{
  "sensorId": "env",
  "readingType": "temperature", 
  "startTime": 0,
  "endTime": 1756833826,
  "readings": []  // <-- TOM ARRAY
}
```

**Problem:** Inga historiska mätningar finns lagrade i systemet.

### 3. Dataformat-mismatch mellan klient och server
**Fil:** `data/script.js:930-931`

**Klientkod förväntar sig:**
```javascript
if (!appState.chart || !data || !data.timestamps || !data.values) {
    return;
}
// ...
appState.chart.data.labels = data.timestamps.map(t => new Date(t));
appState.chart.data.datasets[0].data = data.values;
```

**Server returnerar:**
```json
{
  "readings": []  // Inte timestamps + values
}
```

## Kritisk Undersökning Behövs

### 🔍 PUNKT 3: Datalagring - Hur är det tänkt att fungera?

**VIKTIGT:** Undersök hur historisk sensordata ska lagras och hämtas:

1. **Backend-implementation:**
   - Sök i ESP32-koden efter `/history` endpoint implementation
   - Kontrollera om data lagras i LittleFS eller bara i minnet
   - Verifiera dataformat som skickas från server

2. **Databasfrågor att besvara:**
   - Var lagras historisk sensordata? (LittleFS files, JSON arrays i minnet?)
   - Hur ofta sparas mätningar? (varje 5s som realtidsdata, eller mindre frekvent?)
   - Vilken datastruktur används för lagring?
   - Finns det en retention policy (rensning av gammal data)?

3. **Filer att undersöka:**
   - `src/` katalogen för `/history` endpoint handler
   - `storage/` klasser för datalagring
   - Eventuella config-filer för datalagring

4. **Test att köra:**
   - Låt systemet köra i några timmar
   - Kontrollera om `/history` endpoint börjar returnera data
   - Verifiera att dataformatet matchar client-förväntningarna

## Tekniska Fixes Behövs

### Fix 1: Automatisk dropdown-populering
```javascript
// I data/script.js, function initApp() runt rad 146
// Efter: initChart();
// Lägg till:
updateChartOptions();
```

### Fix 2: Dataformat-kompatibilitet
Antingen:
- **A) Ändra server** för att returnera `{timestamps: [], values: []}` 
- **B) Ändra klient** för att hantera `{readings: []}` format

### Fix 3: Datalagring (efter undersökning)
- Implementera historisk datalagring om den saknas
- Konfigurera lämplig sampling-frekvens för grafer
- Säkerställ att gammal data rensas för att spara utrymme

## Testplan för Lösning

1. **Steg 1:** Fix dropdown-problem och testa manuellt
2. **Steg 2:** Undersök backend-datalagring grundligt  
3. **Steg 3:** Åtgärda dataformat-mismatch
4. **Steg 4:** Implementera/fixa datalagring om nödvändigt
5. **Steg 5:** E2E-test med Playwright för att verifiera graf fungerar

## Observationer från Analys

- **Realtidsdata fungerar perfekt:** BME280 (temp, humidity, pressure) uppdateras var 5:e sekund
- **Jordsensor problem:** "Soil sensor read failed: 3" (separat problem, ej relaterat till graf)
- **Chart.js implementation:** Korrekt implementerad med mörk tema-stöd
- **API endpoints:** `/sensors` och `/status` fungerar, `/history` returnerar tom data

## Referenser

- **Analyserad kod:** `data/script.js:759-986` (Chart implementation)
- **Test endpoint:** `http://192.168.1.123/history?sensor=env&reading=temperature&range=24h`
- **Console logs:** Kontinuerliga "Soil sensor read failed: 3" meddelanden
- **Webbläsare:** Playwright automation i Chrome/Chromium

---
*Detta dokument ska användas som arbetsmaterial när tid finns för att implementera graf-fixes.*