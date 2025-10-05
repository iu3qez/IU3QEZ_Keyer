Specifiche Keyer CW HST - ESP32-S3-AUDIO-Board
Hardware

Board: ESP32-S3-AUDIO-Board
MCU: ESP32-S3 dual-core @ 240MHz
Audio: Codec I2S integrato con amplificatore e altoparlanti
Display: LCD I2C (da aggiungere in seguito per visualizzazione parametri)
Target: Competizioni High Speed Telegraphy (HST)

Ambiente Sviluppo

IDE: PlatformIO
Framework: ESP-IDF o Arduino (da definire)
Toolchain: Espressif ESP32-S3

Input

Tasti meccanici: verticali e paddle (bug)

Velocità limitata (~40 WPM realistici)
Soggetti a bounce meccanico critico


Keyer elettronici/iambic: input già pulito, debounce minimo

Debouncing (solo tasti meccanici)
Median Filter 5-tap

Sample rate: 20 kHz (risoluzione 50μs)
Implementazione: sorting network ottimizzato (6 comparazioni)
Latenza target: <300μs
Posizionamento: Core 0, ISR in IRAM (IRAM_ATTR)

cuint8_t buffer[5];
// Median tramite sorting network durante GPIO interrupt
// Output: segnale pulito per logica Curtis
Ottimizzazioni ESP32-S3

Timer hardware per sampling deterministico
GPIO interrupt-driven
Pin con pull-up interno attivo
Nessun flash cache miss in ISR

Logica Iambic Curtis Mode B Avanzato
Finestra di Memorizzazione (WND)
Controllo preciso di quando il keyer memorizza la pressione del paddle opposto durante generazione elemento corrente.
Parametri configurabili:

U (Up): percentuale elemento per apertura finestra (es. 45%)
D (Down): percentuale elemento per chiusura finestra (es. 5%)

Comportamento:
Durante elemento corrente:
  progress = (tempo_trascorso * 100) / durata_elemento
  
  if (progress >= U && progress <= (100 - D)):
    // Dentro finestra: controlla paddle opposto
    if (paddle_opposto_premuto):
      memorizza_prossimo_elemento
  else:
    // Fuori finestra: ignora paddle opposto
Esempi configurazione:

U=45%, D=5%: finestra centrale (esempio mostrato)
U=30%, D=40%: finestra ampia, memory facile
U=99%, D=40%: finestra ristretta, quasi fine elemento
U=60%, D=1%: finestra molto stretta, memory solo ultimi istanti

Timing Critico

Timer hardware ESP32-S3 (non software delays)
Precisione sub-millisecondo richiesta
Jitter minimale per HST

Output
1. GPIO Output - Keying Radio

Chiusura contatto verso radio (transistor/MOSFET/optoisolatore)
Core 0: generazione timing real-time
Nessuna latenza USB, controllo diretto GPIO
Timing critical con jitter minimale

2. Sidetone Audio

Core 1: generazione tono via codec I2S integrato
Sample rate: 16 kHz (sufficiente per tono CW)
Tono: 600-800 Hz (wavetable precomputata)
Latenza ~8ms (accettabile, solo feedback operatore)
DMA buffer: 2 buffer × 128 samples
Envelope ADSR: attack <1ms, release 5ms
Output su amplificatore e altoparlanti integrati

3. Display LCD I2C (da implementare in seguito)

Core 1: aggiornamento display parametri
Visualizzazione: WPM, Window U/D%, Mode, altri parametri configurabili
Update rate basso (~100ms) per non impattare performance
Tipico: LCD 16x2 o 20x4 caratteri

4. Debug/Telemetria

USB CDC (non HID)
Core 1: output parametri, timing, eventi
Non interferisce con real-time Core 0

Architettura Software
Dual Core Assignment
Core 0 (real-time critical):

Median filter ISR
Element generator (timing preciso)
Memory window logic
GPIO output keying

Core 1 (non-critical):

Codec I2S sidetone
LCD I2C display update
USB CDC debug
Eventuale telemetria

Flusso Dati
GPIO input → Median Filter (Core 0) → Valid edge detection
                                          ↓
                            Element generator + Window logic
                                          ↓
                              ┌───────────┴───────────┐
                              ↓                       ↓
                    GPIO keying output    Queue → Sidetone (Core 1)
                                                      ↓
                                          ┌───────────┴───────────┐
                                          ↓                       ↓
                                    LCD I2C display         USB CDC debug
Comunicazione Inter-Core

FreeRTOS queue tra core (depth=2, zero-copy)
Trigger sidetone via queue dopo edge detection valido
Shared variables per parametri display (mutex-protected)

Configurazione PlatformIO
platformio.ini (esempio base)
ini[env:esp32-s3-audioboard]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino  ; o espidf
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
board_build.flash_mode = qio
board_build.partitions = default.csv

monitor_speed = 115200
monitor_filters = esp32_exception_decoder

build_flags = 
    -DCORE_DEBUG_LEVEL=3
    -DBOARD_HAS_PSRAM
    
lib_deps =
    ; Da definire in base a librerie necessarie (I2C LCD, etc)
Parametri da Definire Successivamente

Framework definitivo (Arduino vs ESP-IDF nativo)
Ulteriori controlli configurabili (weight, compensation, altre modalità)
Metodo di configurazione parametri (encoder rotativo, pulsanti, seriale, web interface)
Gestione preset/profili operatore
Layout display LCD
Librerie specifiche per codec audio board

Performance Target

Latenza totale debouncing: <300μs
Jitter GPIO output: <100μs
Latency sidetone: ~8ms (non critico)
Display update rate: ~100ms (non critico)
Velocità supportata: fino a 60+ WPM con bug meccanici

Note Implementative

Evitare ultrapaddling (problematico in HST)
Finestra WND è ortogonale al debouncing: prima pulisci segnali, poi processi logica
Validazione con oscilloscopio: GPIO input + GPIO output per misurare latenza effettiva
Sfruttare hardware audio integrato della board per sidetone di qualità
LCD I2C su Core 1 per non impattare timing critico Core 0
PlatformIO permette gestione dipendenze e build ottimizzata per target specifico