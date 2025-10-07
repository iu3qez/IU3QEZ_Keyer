# IU3QEZ ESP-IDF Bring-Up

Questo sottoprogetto `idf/` contiene un primo porting nativo ESP-IDF per validare due blocchi fondamentali dell'hardware ESP32-S3-AUDIO-BOARD:

- **Codec audio ES8311** tramite il componente ufficiale Espressif `esp_codec_dev`
- **Expander I2C TCA9555** tramite il componente `esp_io_expander_tca95xx_16bit`

L'applicazione `app_main.c` inizializza il bus I2C, il multiplexer TCA9555 (selezionando la modalità I2C e abilitando il pin dell'amplificatore), configura la periferica I2S come master a 16 kHz e crea un'istanza del codec ES8311. Dopo l'apertura del dispositivo codec viene generato un tono sinusoidale a 600 Hz via `esp_codec_dev_write`, mentre in parallelo viene campionato il pin di prova `P1.1` del TCA9555 per verificare la lettura degli ingressi.

## Struttura

- `CMakeLists.txt` – include i componenti ufficiali presenti in `../idf_samples`
- `main/app_main.c` – logica di test per codec e IO expander
- `sdkconfig.defaults` – preset per flash, stack e console USB-JTAG (coerenti con ESP32-S3 Audio Board)

## Build

1. Assicurati di avere l'ambiente ESP-IDF 5.2 o superiore configurato (`. ./export.sh`).
2. Posizionati nella cartella `idf/`:

   ```bash
   cd idf
   idf.py set-target esp32s3
   idf.py build
   ```

3. Per flashare e aprire il monitor:

   ```bash
   idf.py -p <porta_seriale> flash monitor
   ```

Durante l'esecuzione dovresti udire il tono a 600 Hz sugli altoparlanti integrati. Nel log seriale compaiono inoltre i report periodici dello stato dell'ingresso `P1.1` del TCA9555 (utilizzabile per agganciare uno dei paddle o un pulsante di prova).

## Prossimi Passi

- Sostituire il tono fisso con il generatore sidetone agganciando la macchina a stati del keyer.
- Mappare in modo completo i pin del TCA9555 per i paddle DOT/DASH e l'abilitazione dell'amplificatore.
- Integrare NVS, Wi-Fi e web UI una volta stabilita la base hardware.
