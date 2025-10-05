# Timeline System - Design Document

## Obiettivo
Sistema di visualizzazione real-time degli eventi paddle/output su timeline grafica scrollante per analisi, debug e futuro decoder Morse.

---

## Architettura

### 1. Event Capture (ESP32 - Core 0 ISR)

**Buffer Circolare Lockfree** (`timeline_buffer.h/cpp`):
- **Dimensione**: 1024 eventi (8 KB RAM)
- **Thread-safe**: scrittura da ISR, lettura da WebSocket task
- **Overwrite policy**: eventi vecchi sovrascritti se buffer pieno
- **Performance**: O(1) push/read, zero malloc, IRAM_ATTR per ISR

**Eventi Catturati**:
```cpp
enum TimelineEventType {
    EVENT_DOT_PRESS     = 0x01,  // DOT paddle premuto
    EVENT_DOT_RELEASE   = 0x02,  // DOT paddle rilasciato
    EVENT_DASH_PRESS    = 0x04,  // DASH paddle premuto
    EVENT_DASH_RELEASE  = 0x08,  // DASH paddle rilasciato
    EVENT_KEY_ON        = 0x10,  // Output KEY attivato
    EVENT_KEY_OFF       = 0x20,  // Output KEY disattivato
};

enum TimelineEventFlags {
    FLAG_NONE           = 0x00,
    FLAG_IAMBIC         = 0x01,  // Squeeze iambic (entrambi paddle)
    FLAG_MEMORY_LATCH   = 0x02,  // Memory latch triggerato
    FLAG_DEBOUNCE_SKIP  = 0x04,  // Evento scartato da debouncing
};

struct TimelineEvent {
    uint32_t timestamp_us;       // Timestamp microsecondi
    TimelineEventType type;      // Tipo evento
    TimelineEventFlags flags;    // Flags aggiuntivi
    uint8_t reserved[2];         // Padding (8 bytes totali)
};
```

**Punti di cattura** (in `keyer_logic.cpp`):
1. `timerISR()` - DOT/DASH press/release (dopo debouncing)
2. `startElement()` - KEY_ON quando inizia DOT/DASH
3. `processNextElement()` - KEY_OFF quando termina elemento
4. State machine - KEY_OFF in inter-element space

---

### 2. WebSocket Streaming (ESP32 - Core 1)

**Endpoint**: `ws://[ESP32_IP]/ws/timeline`

**Protocollo**:
- **Formato**: JSON array di eventi
- **Update rate**: configurabile (50-200ms default)
- **Batch size**: max 100 eventi per messaggio

**Esempio payload**:
```json
{
  "events": [
    {"ts": 1234567890, "type": "DOT_PRESS", "flags": []},
    {"ts": 1234568950, "type": "DOT_RELEASE", "flags": []},
    {"ts": 1234569000, "type": "KEY_ON", "flags": []},
    {"ts": 1234570000, "type": "KEY_OFF", "flags": []},
    {"ts": 1234571000, "type": "DASH_PRESS", "flags": ["IAMBIC"]},
  ],
  "buffer_stats": {
    "total_pushed": 12345,
    "total_dropped": 0,
    "overruns": 0,
    "available": 42
  }
}
```

---

### 3. Frontend Timeline Renderer (Browser - Canvas HTML5)

**Visualizzazione**:
```
┌─────────────────────────────────────────────────────────┐
│  Timeline Visualization (scrolling ←)                    │
├─────────────────────────────────────────────────────────┤
│                                                           │
│  DOT Paddle:   ▁▁▁███▁▁▁▁▁▁███▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁      │  (blu)
│                                                           │
│  DASH Paddle:  ▁▁▁▁▁▁▁▁▁███▁▁▁▁▁▁███▁▁▁▁▁▁▁▁▁▁▁      │  (rosso)
│                                                           │
│  OUTPUT Key:   ▁▁▁███▁███▁▁▁███▁███▁▁▁▁▁▁▁▁▁▁▁▁      │  (verde)
│                                                           │
│                ◄─────────────────────────────────────►    │
│                0s        2s        4s        6s           │
└─────────────────────────────────────────────────────────┘
           OLD ←──────  time  ──────→ NEW (current)
```

**Comportamento Scorrimento**:
1. **Eventi nuovi** appaiono a **destra** (current time)
2. Timeline scorre **verso sinistra** quando eventi raggiungono **centro pagina**
3. Eventi **vecchi** escono fuori campo a **sinistra**

**Scala Temporale**:
- Basata su **WPM corrente** del keyer
- Esempio: 20 WPM → 1 DOT = 60ms
- **Zoom configurabile**: 5-20 secondi visibili

**Rendering**:
- **Canvas HTML5** (2D context)
- **Frame rate**: 30-60 FPS
- **Anti-aliasing**: smooth edges
- **Color coding**:
  - DOT paddle: `#3498db` (blu)
  - DASH paddle: `#e74c3c` (rosso)
  - OUTPUT: `#27ae60` (verde)
  - Iambic highlight: `#f39c12` (arancione bordo)

**Canvas Layout**:
```javascript
const CANVAS_HEIGHT = 200;
const ROW_HEIGHT = 50;
const ROW_MARGIN = 10;

// Righe
const DOT_ROW_Y = 10;
const DASH_ROW_Y = 70;
const OUTPUT_ROW_Y = 130;

// Stati
const STATE_HIGH = 10;   // Premuto/ON
const STATE_LOW = 40;    // Rilasciato/OFF
```

---

### 4. Parametri Configurabili

**Frontend** (`index.html`):
- **Timeline Duration**: 5-20 secondi (slider)
- **Update Rate**: 50-200ms (slider)
- **Enable/Disable**: checkbox per attivare timeline

**Backend** (future):
- Buffer size (compile-time)
- Event filters (runtime)

---

## Implementazione Timeline Frontend

### 4.1 Struttura Dati Client

```javascript
class TimelineRenderer {
    constructor(canvasId, config) {
        this.canvas = document.getElementById(canvasId);
        this.ctx = this.canvas.getContext('2d');
        this.config = {
            duration: 10,        // secondi visibili
            updateRate: 100,     // ms tra updates
            wpm: 20,             // WPM corrente
            ...config
        };

        this.events = [];        // Array eventi con timestamp relativo
        this.currentTime = 0;    // Tempo corrente (ms)
        this.scrollOffset = 0;   // Offset scorrimento

        this.initCanvas();
        this.startAnimation();
    }

    // Ricevi eventi da WebSocket
    addEvents(newEvents) {
        this.events.push(...newEvents);
        this.cleanOldEvents();
    }

    // Render loop
    render() {
        this.clearCanvas();
        this.drawTimeAxis();
        this.drawPaddleRow('DOT', this.events);
        this.drawPaddleRow('DASH', this.events);
        this.drawOutputRow(this.events);
        this.drawIambicHighlights(this.events);
    }

    // Scorrimento automatico
    updateScroll() {
        if (this.currentTime > this.config.duration / 2) {
            this.scrollOffset = this.currentTime - (this.config.duration / 2);
        }
    }
}
```

---

## Utilizzi Futuri

### 5.1 Decoder Morse
- Analisi pattern temporali DOT/DASH
- Riconoscimento caratteri
- Text output real-time

### 5.2 Analisi Statistiche
- Timing precision (jitter DOT/DASH)
- Iambic squeeze detection
- WPM effettivo vs target

### 5.3 Recording/Replay
- Save sessioni su file
- Replay per training
- Export formato standard (JSON/CSV)

### 5.4 Debug Avanzato
- Paddle bouncing detection
- Memory latch timing analysis
- Debounce tuning

---

## Note Implementazione

### Thread Safety
- **ISR write**: atomic head increment
- **Task read**: atomic tail increment
- **No locks**: lockfree garantito
- **Memory barrier**: ESP32 hardware guarantee

### Performance Target
- **ISR latency**: < 10μs per evento
- **Buffer overhead**: < 0.5% CPU @ 60 WPM
- **WebSocket throughput**: < 1 KB/s @ 60 WPM
- **Frontend render**: 30-60 FPS smooth

### Memory Budget
- **Buffer**: 8 KB (1024 eventi)
- **WebSocket**: ~2 KB/message max
- **Frontend**: negligible (canvas rendering)

---

## Status Implementazione

- [x] Buffer circolare lockfree (`timeline_buffer.h/cpp`)
- [x] Event capture in KeyerLogic ISR
- [ ] WebSocket endpoint (`/ws/timeline`)
- [ ] Frontend Canvas renderer
- [ ] Configurazione timeline (duration, rate)
- [ ] Test con paddle reali
- [ ] Decoder Morse (future)

---

**Ultima modifica**: 2025-10-05
**Autore**: Claude + IU3QEZ
