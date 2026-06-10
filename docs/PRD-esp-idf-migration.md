# PRD: WateringSystem firmware-migrering Arduino → ESP-IDF

**Status:** Utkast 1.0 (2026-06-09)
**Beställare:** Paul Waserbrot
**Utförare:** AI-agenter (Claude Code / Opus / Sonnet / Fable) med Paul som granskare och beslutsfattare
**Relaterade dokument:** `docs/architecture.md`, `docs/hardware.md`, `hardware/bom/WateringSystem-rev2-BOM.md`, `docs/future-improvements.md`

---

## Översikt

Ny firmware för WateringSystem byggd på ren ESP-IDF (C++), utvecklad parallellt i `firmware/` i befintligt repo. Arduino/PlatformIO-firmwaren fryses (git-tag `arduino-final`) och fortsätter driva växthusenheten orörd tills rev2-hårdvaran tas i drift. Arbetsflödet designas för AI-driven utveckling: Docker-byggen, CI som producerar firmware-releaser, host-baserade tester, och OTA-distribution — Paul ska aldrig behöva bygga lokalt eller flasha med kabel efter initial bring-up.

## Problem

- Arduino/PlatformIO kräver lokal byggkedja som inte längre används aktivt; utvecklingen sker av AI-agenter och behöver reproducerbara container-byggen + CI.
- Arduino-lagret abstraherar bort ESP32-förmågor projektet behöver: riktig A/B-OTA med rollback, esp-modbus, hårdvaru-task-watchdog, JTAG-debug, Kconfig.
- Rev2-hårdvaran (eget kort, THVD1426 auto-direction, INA226, CP2102N/USB-C, JTAG-header) är designad med ESP-IDF i åtanke.
- Hacket i platformio.ini (`-UHTTP_GET` m.fl.) är symtom på ramverkskonflikter som försvinner med esp_http_server.

## Användare

- **Paul (operatör/beställare):** beställer funktioner, granskar, fattar beslut, sköter KiCad/rev2. Interagerar med systemet via webbgränssnitt och GitHub.
- **AI-agenter (utvecklare):** skriver, bygger, testar och släpper firmware via Docker/CI. `firmware/CLAUDE.md` är deras primära kontext.
- **Slutanvändning:** automatisk växthusbevattning, samma som idag.

## Omfattning

### Ingår

- Funktionsparitet med Arduino-firmware v2.3: bevattningslogik, schemaläggning, reservoarstyrning med nivåsensorer, manuellt/automatiskt läge, sensorvalidering med fail-safe pumpstopp, webbgränssnitt, diagnostik, WiFi-återanslutning.
- Det rev2-hårdvaran *kräver*: INA226-drivrutin (mätning/loggning av pumpström — rådata, inga nya skyddsfunktioner), inverterad XKC-Y26-nivåsensorlogik, DE-lös RS485-drift.
- Moderniserat web-API under `/api/v1/`.
- Kconfig-kortabstraktion `BOARD_REV1_DEVKIT` / `BOARD_REV2`.
- Bygg/test/release-infrastruktur (Docker, GitHub Actions, host-tester, OTA).

### Ingår EJ

- Nya säkerhetsfunktioner ovanpå INA226 (torrkörnings-/blockeringsdetektering) → egen PRD efter paritet.
- Frontend-modernisering → egen PRD; detta projekt levererar API-kontraktet den bygger mot.
- In-place-uppgradering av växthusenheten; migrering av LittleFS-historik eller wifi_config.json (beslut: börja om rent på rev2).
- Rev2-schemaritning/PCB-layout (parallellt hårdvaruspår, se Processplan).
- MCU-byte (ESP32-WROOM-32E behålls) [ANTAGANDE].

## Funktionella krav

- [ ] FR1: All bevattningslogik från `WateringController` portad med bibehållet beteende (paritetschecklista, se Framgångskriterier).
- [ ] FR2: Modbus RTU-kommunikation med jordsensorn (adress 0x01, 9600 8N1, registerkarta 0x0000–0x0007 [ANTAGANDE: oförändrad]) via esp-modbus.
- [ ] FR3: BME280 via I2C (nytt i2c_master-API).
- [ ] FR4: Pumpstyrning via GPIO/MOSFET med max-körtider, fail-safe-stopp vid ogiltig sensordata i autoläge, manuellt override. Rev1 (rigg/växthus idag): två pumpar (bevattning + reservoarpåfyllning). Rev2 (nod): **EN pumpkanal** — reservoarpåfyllning flyttar till en framtida central reservoar-enhet (multi-zon, egen PRD; se `docs/feature-ideas.md`); under mellanperioden fylls växthusnodens reservoar manuellt. Firmware exponerar pumpuppsättningen via board-kapabilitetsflagga. [BESLUT 2026-06-10]
- [ ] FR5: Reservoarstyrning med två nivåsensorer; polaritet styrd av board-konfiguration. Styrlogiken (fyll vid låg, stopp vid hög, 300 s maxtid) är aktiv endast på boards med lokal påfyllnadspump (rev1); på rev2 används nivåsensorerna för status och framtida påfyllnadsbegäran (multi-zon). [BESLUT 2026-06-10, se FR4] Polaritetsfakta (från fix-branch 2026-04-12, verifierad mot KiCad-schema): XKC-Y26 OUT är aktiv HÖG (vatten = HIGH). Rev1 (direkt via TXS0108E, icke-inverterande) ⇒ GPIO aktiv HÖG. Rev2 (via 2N7002-inverterare) ⇒ GPIO aktiv LÅG. OBS: Arduino-koden läser aktiv LÅG på rev1 — känd bugg; verifiera slutgiltig polaritet med mätning på bänkriggen i fas 1 och dokumentera i paritetschecklistan (korrekt beteende, inte bug-för-bug-paritet).
- [ ] FR6: INA226-läsning för pumpkanalen på rev2 (spänning/ström/effekt), exponerad i API och loggning. (En INA226 — rev2 har en pumpkanal per FR4-beslutet.)
- [ ] FR7: Web-API `/api/v1/` — konsekvent REST/JSON: status, sensordata, historik, pumpstyrning, konfiguration, systeminfo, OTA-trigger. API-kontrakt dokumenteras (OpenAPI-skiss) och fryses som leverans.
- [ ] FR8: Webbassets serveras från littlefs; befintlig frontend anpassas minimalt till nya API:t som testklient (full modernisering = egen PRD).
- [ ] FR9: WiFi-provisioning vid okonfigurerad enhet [KANDIDAT: egen SoftAP-portal portad / IDF:s wifi_provisioning-komponent — välj på robusthet och enkelhet i fas 2].
- [ ] FR10: SNTP-tidssynk (svensk pool), tidszonshantering CET/CEST.
- [ ] FR11: OTA: A/B-app-partitioner, automatisk rollback vid misslyckad boot-verifiering, hämtning från GitHub Releases via esp_https_ota samt manuell uppladdning via webbgränssnittet som fallback.
- [ ] FR12: Diagnostik motsvarande dagens serial-kommandon, åtkomlig via seriell konsol och API.
- [ ] FR13: Konfiguration i NVS; rimliga defaultvärden vid fabriksåterställning.

## Icke-funktionella krav

- **Arkitektur:** C++ (C++17 eller senare) med befintlig interfacearkitektur (`ISensor`, `IEnvironmentalSensor`, `ISoilSensor`, `IActuator`, `IWaterPump`, `IModbusClient`, `IDataStorage`); Arduino `String` ersätts med `std::string`. Drivrutinsimplementationer mot rena IDF-API:er.
- **Byggbarhet:** ESP-IDF [KANDIDAT: senaste stabila vid fas 0-start], pinnad i Dockerfile. `idf.py build` ska fungera identiskt lokalt-i-container och i CI. Inga beroenden utanför IDF Component Registry + repo.
- **Testbarhet:** Applikationslogik testbar på host (IDF Linux-target) mot mock-implementationer av interfacen; testsvit körs i CI på varje push. Mål: bevattningslogik och säkerhetsvillkor 100% host-testade.
- **Tillförlitlighet:** esp_task_wdt på alla kritiska tasks; graceful degradation vid sensorbortfall (paritet med dagens beteende); WiFi-bortfall får aldrig påverka bevattningssäkerhet.
- **Säkerhet (drift):** pumpar alltid av vid boot, efter watchdog-reset och vid OTA-omstart. Fail-safe-beteenden definieras i paritetschecklistan och host-testas.
- **Loggning:** ESP_LOG med nivåer per komponent; viktiga händelser (pumpstart/stopp, fel, OTA) persisteras.
- **Licens/öppenhet:** AGPL-3.0-or-later behålls. Repot publiceras publikt [BESLUT KRÄVS: genomgång av historik för känsligt innehåll före publicering — krav för FR11/GitHub-OTA].

## Processplan — två parallella spår

```
Spår A (AI): FIRMWARE          Spår B (Paul): HÅRDVARA REV2
─────────────────────          ────────────────────────────
Fas 0  Skelett/CI/Docker       Schema i KiCad (rev2)
Fas 1  Drivrutinslager   ◄──── SYNK 1: pinmappning fryses
Fas 2  Infrastruktur            Layout, DRC
Fas 3  Web-API v1               Beställning JLCPCB
Fas 4  Applikationslogik        (ledtid tillverkning)
Fas 5  OTA & release
Fas 6  Rev2 bring-up      ◄──── SYNK 2: kort levererade
```

Spåren är oberoende utom vid SYNK 1 och SYNK 2. Fas 1–5 utvecklas och verifieras på rev1-devkit-riggen. Växthusenheten rörs aldrig; den ersätts av rev2-enheten när fas 6 är godkänd.

**Pauls startpunkter:** (1) bygg/komplettera bänkriggen (devkit + RS485 + jordsensor), (2) rev2-schema i KiCad, (3) godkänn fas 0-start.

### Fasdefinitioner med slutkriterier

| Fas | Innehåll | Klart när |
|---|---|---|
| 0 | `firmware/`-projekt, components-struktur, Kconfig-boardval, Dockerfile, GitHub Actions (build), ny root- + `firmware/`-CLAUDE.md, spec-kit-init (`.specify/`, agenter, kommandon – portat från ev-charging-manager), konstitution via `speckit.constitution`, PRD→PR-nedbrytning med beroendegraf i `docs/prd/`. Legacy-frys: branch `arduino-maintenance` + tag `arduino-final` med pinnade PlatformIO-beroenden, CI-jobb som bygger legacy-branchen och sparar flashbar .bin som artifact, legacy-CLAUDE.md med underhållsbanner | CI bygger grön binär för båda board-targets från ren checkout; CI bygger även `arduino-maintenance` reproducerbart |
| 1 | esp-modbus/UART, BME280/I2C, pump-GPIO, nivåsensorer, INA226-drivrutin, board-pinmappningar | HIL på rigg: sensordata läses korrekt, pumpar styrs, riggens diagnostik OK. [BEROENDE AV SYNK 1 för slutgiltig REV2-tabell] |
| 2 | NVS-konfig, littlefs, WiFi + provisioning (FR9-beslut tas här), SNTP, esp_task_wdt, loggning | Rigg ansluter, överlever WiFi-bortfall/återanslutning, watchdog-test passerar, tid korrekt |
| 3 | esp_http_server, `/api/v1/`-design + OpenAPI-skiss, frontend minimalt anpassad | API-kontrakt dokumenterat; alla endpoints svarar enligt kontrakt; frontend fungerar mot riggen |
| 4 | WateringController-port, host-testsvit i CI, paritetschecklista | Testsvit grön i CI; paritetschecklista avbockad på rigg |
| 5 | A/B-partitioner, rollback-verifiering, esp_https_ota mot GitHub Releases, release-workflow | Riggen uppdaterar sig själv från en GitHub-release OCH rollbackar automatiskt en avsiktligt trasig build |
| 6 | `BOARD_REV2`-validering: THVD1426 auto-direction @ 9600 baud, INA226-mätning, inverterade nivåsensorer, CP2102N-flash, JTAG-rök­test; fältdriftsättning | Rev2-enhet kör i växthuset ≥ 2 veckor utan ingrepp; Arduino-enheten pensioneras |

## Framgångskriterier

- Växthusenheten ersatt av rev2 + ESP-IDF; därefter sker alla uppdateringar via OTA utan kabel.
- Paul har inte behövt köra ett enda lokalt bygge under projektet.
- Paritetschecklista (tas fram i fas 0 ur Arduino-koden, granskas av Paul) 100% avbockad — inklusive alla fail-safe-beteenden.
- Host-testsvit täcker bevattningslogik + säkerhetsvillkor och körs grönt i CI.
- API-kontrakt `/api/v1/` dokumenterat och fryst → input till frontend-PRD.
- `git checkout arduino-final` ger för alltid en byggbar referens av legacy-firmwaren.

## Öppna frågor

- [ ] FR9: provisioning-val (egen portal vs wifi_provisioning) — avgörs i fas 2 efter teknisk spik.
- [ ] Repo-publicering: genomgång av git-historik för känsligt innehåll (lösenord, AP_PASSWORD finns i källkod idag!) före publik release. [BESLUT KRÄVS]
- [ ] Partitionslayout: ryms 2 × app + NVS + littlefs i 4MB? Kalkyl i fas 0; gzippade assets; fallback = N8/N16-modul på rev2 (BOM-ändring). [ANTAGANDE: 4MB räcker]
- [ ] Ska Arduino-koden flyttas till `legacy/` vid paritet eller raderas (historiken finns i taggen)?
- [ ] THVD1426: verifiera driver-release/idle-beteende vid 9600 baud mot datablad + på kort (fas 6, redan noterat i rev2-BOM).
- [ ] Diagnostik via API (FR12): exakt omfattning — definieras i fas 3.

## Antaganden

- Bänkrigg med rev1-devkit + RS485 + jordsensor finns tillgänglig från fas 1.
- NPK-sensorns registerkarta är oförändrad mot `docs/hardware.md`.
- Rev2 behåller ESP32-WROOM-32E (N4) — flashstorlek omprövas endast om partitionskalkylen kräver det.
- GitHub Releases är acceptabel distributionskanal (kräver publikt repo eller token).

## Arbetssätt (från ev-charging-manager, anpassat för embedded)

- Spec-kit-driven utveckling: varje PR får `specs/NNN-*/` (spec, plan, tasks, checklists) genererad från sin `docs/prd/PR-NN-*.md`.
- Orkestrering: Opus orkestrerar, kodar aldrig i huvudsessionen; `researcher`/`implementer`/`fixer`-subagenter. Checkpoint 1 (clarify, vid frågor), Checkpoint 2 (plan/tasks, alltid stopp), Checkpoint 3 (review, alltid stopp).
- `pr-review-toolkit` körs före varje commit; merge commits (aldrig squash); en branch åt gången; `git worktree` vid parallellism.
- Implementer levererar alltid testchecklista: host-test-PR:er verifieras av CI; hårdvarunära PR:er får HIL-checklista som Paul kör på riggen vid CP3.
- Språk: engelska för commits/kod/docs/issues (publikt repo), svenska i dialogen. `.github/copilot-instructions.md` uppdateras.
- Legacy-patch-flöde: `git worktree add ../WateringSystem-arduino arduino-maintenance` → fixa → bygg (CI/PlatformIO, pinnade versioner) → deploy via kabel/seriell flash (Arduino-firmwaren saknar OTA-endpoint) → tagga `arduino-v2.3.x`. Mergas aldrig till main.

## Efterföljande PRD:er (utanför scope, i prioritetsordning)

1. INA226-baserade skyddsfunktioner (torrkörning, blockering, strömtrend).
2. Frontend-modernisering mot `/api/v1/`.
3. (Från future-improvements.md: kandidater som MQTT, fler sensorer på bussen — omprövas efter driftsättning.)
