# IU3QEZ CW HST Keyer - Web Interface Implementation Plan

## Obiettivo
Implementare una pagina web dinamica e veloce (accessibile via WiFi) che non interferisca con il funzionamento del keyer CW.

---

## Requisiti Funzionali

### 1. Configurazione Parametri
- [ ] **WPM** (Words Per Minute): 5-100
- [ ] **Memory Window UP**: 0-99% (apertura finestra Curtis Mode B)
- [ ] **Memory Window DOWN**: 0-99% (chiusura finestra)
- [ ] **Paddle Debounce**: microsecondi
- [ ] **Volume Sidetone**: 0-100%
- [ ] **Sidetone Frequency**: 300-800 Hz 
- [ ] **Weighting**: ratio DOT/DASH (da implementare in futuro)

### 2. Timeline Real-Time (5-10 parole, ~5-10 secondi @ 20 WPM)
Visualizzazione grafica su asse temporale:
- [ ] **DOT Paddle**: riga timeline con eventi press/release
- [ ] **DASH Paddle**: riga timeline con eventi press/release
- [ ] **Output KEY**: riga timeline con eventi on/off
- [ ] **Iambic Events**: highlight quando entrambi i paddle sono premuti
- [ ] **Memory Latch**: indicazione quando viene triggerato

### 3. Decoder Morse (implementazione futura)
- [ ] Decodifica caratteri Morse in tempo reale
- [ ] Visualizzazione testo decodificato

---

## Architettura Tecnica

### Web Server
- **Framework**: ESPAsyncWebServer (non-blocking)
- **Core**: Core 1 (stesso task audio, priorità bassa)
- **Storage**: SPIFFS per file HTML/CSS/JS
- **Protocol**: HTTP + WebSocket per real-time updates

### Timeline Events Buffer
- **Tipo**: Buffer circolare lockfree
- **Dimensione**: ~500-1000 eventi (5-10 sec @ 50ms granularità)
- **Accesso**:
  - Scrittura da ISR Core 0 (lockfree)
  - Lettura da WebSocket task Core 1
- **Struttura evento**:
  ```cpp
  struct TimelineEvent {
    uint32_t timestamp_us;
    uint8_t type;  // DOT_PRESS, DOT_RELEASE, DASH_PRESS, DASH_RELEASE, KEY_ON, KEY_OFF
    uint8_t flags; // IAMBIC, MEMORY_LATCH, etc.
  };
  ```

### API REST Endpoints
```
GET  /api/config          - Ottieni configurazione corrente
POST /api/config          - Aggiorna configurazione
GET  /api/status          - Stato keyer (WPM, mode, ecc.)
WS   /ws/timeline         - WebSocket per eventi timeline real-time
```

---

## Implementation Steps

### Phase 1: Infrastructure Setup ✅
- [x] 1.1 Aggiungere dipendenze PlatformIO
  - [x] ESPAsyncWebServer
  - [x] AsyncTCP
  - [x] LittleFS
- [x] 1.2 Configurare partizioni LittleFS
- [x] 1.3 Setup WiFi (AP + STA mode con fallback)
- [x] 1.4 Inizializzare AsyncWebServer su Core 1
- [x] 1.5 API REST base (GET /api/status, GET/POST /api/config)
- [x] 1.6 Pagina web fallback HTML
- [x] 1.7 Test funzionamento base

### Phase 2: Timeline Events System
- [ ] 2.1 Creare struct TimelineEvent
- [ ] 2.2 Implementare buffer circolare lockfree
- [ ] 2.3 Integrare eventi nel KeyerLogic (ISR)
  - [ ] DOT paddle press/release
  - [ ] DASH paddle press/release
  - [ ] KEY output on/off
  - [ ] Iambic detection
  - [ ] Memory latch trigger
- [ ] 2.4 Creare WebSocket endpoint `/ws/timeline`
- [ ] 2.5 Task Core 1 per inviare eventi via WebSocket

### Phase 3: Configuration API
- [ ] 3.1 Creare classe ConfigManager
- [ ] 3.2 Implementare serializzazione JSON (ArduinoJson)
- [ ] 3.3 Endpoint GET `/api/config`
- [ ] 3.4 Endpoint POST `/api/config` con validazione
- [ ] 3.5 Applicare configurazione a KeyerLogic runtime
- [ ] 3.6 Persistenza configurazione (SPIFFS/NVS)

### Phase 4: Frontend Web Interface
- [ ] 4.1 HTML structure (single-page app)
- [ ] 4.2 CSS styling (responsive, mobile-friendly)
- [ ] 4.3 JavaScript:
  - [ ] WebSocket client per timeline
  - [ ] Canvas/SVG per rendering timeline grafico
  - [ ] Form configurazione con validazione
  - [ ] REST API client (fetch)
- [ ] 4.4 Timeline rendering:
  - [ ] 3 righe parallele (DOT, DASH, OUTPUT)
  - [ ] Asse temporale scorrevole (ultimi 10 sec)
  - [ ] Color coding (DOT=blu, DASH=rosso, OUTPUT=verde)
  - [ ] Highlight rosso per iambic overlap
  - [ ] Indicatori memory latch
- [ ] 4.5 Upload file su SPIFFS

### Phase 5: Testing & Optimization
- [ ] 5.1 Test latency keyer (verificare nessuna regressione)
- [ ] 5.2 Test jitter audio (verificare audio task non compromesso)
- [ ] 5.3 Test carico WiFi (multi-client)
- [ ] 5.4 Ottimizzazione WebSocket update rate (50-100ms)
- [ ] 5.5 Profiling memoria (heap fragmentation)
- [ ] 5.6 Test stress (keyer @ 60 WPM + web active)

### Phase 6: Future Enhancements
- [ ] 6.1 Implementare weighting control
- [ ] 6.2 Decoder Morse real-time
- [ ] 6.3 Logging sessioni (statistiche WPM, errori)
- [ ] 6.4 OTA firmware update
- [ ] 6.5 Multi-language support (IT/EN)

---

## Performance Requirements

- [ ] Persist audio/keyer configuration in NVS (follow-up to new config module)

### Latency Constraints
- **Keyer ISR**: max 50 μs (nessuna regressione)
- **Audio task jitter**: < 1 ms (nessun glitch audio)
- **WebSocket update**: 50-100 ms (non critico)

### Resource Budget
- **RAM**: < 100 KB per web server + buffers
- **SPIFFS**: ~500 KB per frontend files
- **CPU Core 0**: keyer ISR (non toccato)
- **CPU Core 1**: audio task (priorità 10) + web server (priorità 1)

---

## Technical Notes

### Core Assignment
```
Core 0: Timer ISR (keyer logic) + GPIO ISR (paddle debounce)
Core 1: I2S audio task (priorità 10) + AsyncWebServer (priorità 1) + WebSocket task
```

### WiFi Configuration
- **Mode**: Access Point (AP)
- **SSID**: `IU3QEZ-Keyer`
- **Password**: configurabile
- **IP**: 192.168.4.1 (default ESP32 AP)
- **Channel**: 1 (2.4 GHz)

### Data Flow
```
ISR (Core 0) → Lockfree Buffer → WebSocket Task (Core 1) → Client Browser
User Input (Browser) → REST API → ConfigManager → KeyerLogic (runtime update)
```

---

## Status
- **Current Phase**: Phase 1 - Infrastructure Setup
- **Started**: 2025-10-05
- **Target Completion**: TBD

---

## References
- Codice esistente: `src/keyer_logic.{h,cpp}`
- Configurazione: `src/settings.h`
- Main loop: `src/main.cpp`
- Board: ESP32-S3-AUDIO-BOARD (240 MHz, dual-core)
