#include "web_server.h"
#include "keyer_logic.h"
#include "sidetone_generator.h"
#include "config_manager.h"
#include <ArduinoJson.h>

WebServerManager::WebServerManager(KeyerLogic* keyer, SidetoneGenerator* sidetone, ConfigManager* configMgr)
    : _server(80), _ws("/ws/timeline"), _keyer(keyer), _sidetone(sidetone), _configMgr(configMgr), _wsTaskHandle(NULL) {
}

bool WebServerManager::begin() {
    Serial.println("\n=== Web Server Setup ===");

    // Inizializza LittleFS per file statici
    if (!LittleFS.begin(true)) {  // true = format if mount fails
        Serial.println("ERRORE: LittleFS mount fallito!");
        return false;
    }
    Serial.println("LittleFS mounted");

    // Setup WebSocket
    _ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                    void *arg, uint8_t *data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            Serial.printf("WebSocket client #%u connected\n", client->id());
        } else if (type == WS_EVT_DISCONNECT) {
            Serial.printf("WebSocket client #%u disconnected\n", client->id());
        }
    });
    _server.addHandler(&_ws);

    // Setup routes
    setupRoutes();

    // Avvia server
    _server.begin();
    Serial.println("Web Server avviato su porta 80");

    // Avvia WebSocket task su Core 1 (priorità bassa, dopo audio)
    xTaskCreatePinnedToCore(
        wsTask,             // Function
        "WS_Timeline",      // Name
        4096,               // Stack size
        this,               // Parameter
        1,                  // Priority (bassa, sotto audio task)
        &_wsTaskHandle,     // Handle
        1                   // Core 1
    );
    Serial.println("WebSocket timeline task avviato su Core 1");

    return true;
}

void WebServerManager::setupRoutes() {
    // Serve static files FIRST (più veloce)
    _server.serveStatic("/", LittleFS, "/")
        .setDefaultFile("index.html")
        .setCacheControl("max-age=600");  // Cache 10 minuti

    // API: Get status
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->handleGetStatus(request);
    });

    // API: Get config
    _server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->handleGetConfig(request);
    });

    // API: Save config to NVS
    _server.on("/api/config/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
        this->handleSaveConfig(request);
    });

    // API: Reset config to defaults
    _server.on("/api/config/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        this->handleResetConfig(request);
    });

    // API: Post config (with body handler)
    _server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            // Risposta inviata nel body handler
        },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            // Body handler - riceve JSON
            static String body;

            if (index == 0) {
                body = "";
            }

            for (size_t i = 0; i < len; i++) {
                body += (char)data[i];
            }

            // Processamento completo quando abbiamo tutto il body
            if (index + len == total) {
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, body);

                if (error) {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
                    return;
                }

                // Validazione e applicazione configurazione
                bool modified = false;

                if (doc.containsKey("wpm")) {
                    uint8_t wpm = doc["wpm"];
                    if (wpm >= 5 && wpm <= 100) {
                        _keyer->setWPM(wpm);
                        modified = true;
                    }
                }

                if (doc.containsKey("window_up") && doc.containsKey("window_down")) {
                    uint8_t up = doc["window_up"];
                    uint8_t down = doc["window_down"];
                    if (up <= 99 && down <= 99) {
                        _keyer->setMemoryWindow(up, down);
                        modified = true;
                    }
                }

                if (doc.containsKey("mode")) {
                    uint8_t mode = doc["mode"];
                    if (mode <= 3) {
                        _keyer->setMode(mode);
                        modified = true;
                    }
                }

                if (doc.containsKey("debounce")) {
                    uint32_t debounce = doc["debounce"];
                    if (debounce >= 100 && debounce <= 10000) {
                        _keyer->setDebounce(debounce);
                        modified = true;
                    }
                }

                if (doc.containsKey("volume")) {
                    uint8_t volume = doc["volume"];
                    if (volume <= 100) {
                        _sidetone->setVolume(volume);
                        modified = true;
                    }
                }

                if (doc.containsKey("frequency")) {
                    uint16_t freq = doc["frequency"];
                    if (freq >= 300 && freq <= 800) {
                        _sidetone->setFrequency(freq);
                        modified = true;
                    }
                }

                // Risposta
                JsonDocument response;
                response["success"] = modified;
                response["message"] = modified ? "Configuration updated" : "No valid parameters";

                String output;
                serializeJson(response, output);
                request->send(200, "application/json", output);
            }
        }
    );

    // 404 handler
    _server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not Found");
    });
}

void WebServerManager::handleGetStatus(AsyncWebServerRequest *request) {
    JsonDocument doc;

    doc["wpm"] = _keyer->getWPM();
    doc["mode"] = _keyer->getMode();
    doc["state"] = _keyer->getState();
    doc["keying"] = _keyer->isKeying();
    doc["dot_pressed"] = _keyer->getDotPressed();
    doc["dash_pressed"] = _keyer->getDashPressed();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServerManager::handleGetConfig(AsyncWebServerRequest *request) {
    JsonDocument doc;

    doc["wpm"] = _keyer->getWPM();
    doc["mode"] = _keyer->getMode();

    uint8_t up, down;
    _keyer->getMemoryWindow(&up, &down);
    doc["window_up"] = up;
    doc["window_down"] = down;

    doc["debounce"] = _keyer->getDebounce();
    doc["volume"] = _sidetone->getVolume();
    doc["frequency"] = _sidetone->getFrequency();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebServerManager::handleSaveConfig(AsyncWebServerRequest *request) {
    // Leggi configurazione corrente da keyer e sidetone
    _configMgr->readFromKeyer(_keyer);
    _configMgr->readFromSidetone(_sidetone);

    // Salva in NVS
    bool success = _configMgr->save();

    JsonDocument response;
    response["success"] = success;
    response["message"] = success ? "Configuration saved to flash" : "Save failed";

    String output;
    serializeJson(response, output);
    request->send(success ? 200 : 500, "application/json", output);
}

void WebServerManager::handleResetConfig(AsyncWebServerRequest *request) {
    // Reset a defaults
    _configMgr->resetToDefaults();

    // Applica ai componenti
    _configMgr->applyToKeyer(_keyer);
    _configMgr->applyToSidetone(_sidetone);

    // Salva in NVS
    _configMgr->save();

    JsonDocument response;
    response["success"] = true;
    response["message"] = "Configuration reset to defaults";

    String output;
    serializeJson(response, output);
    request->send(200, "application/json", output);
}

void WebServerManager::handlePostConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Gestito da body handler in setupRoutes()
}

void WebServerManager::wsTask(void* parameter) {
    WebServerManager* self = (WebServerManager*)parameter;

    Serial.println("WebSocket task started");

    while (true) {
        // Invia eventi timeline ogni 100ms
        self->sendTimelineEvents();

        // Cleanup connessioni chiuse
        self->_ws.cleanupClients();

        // Delay 100ms
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void WebServerManager::sendTimelineEvents() {
    // Check se ci sono client connessi
    if (_ws.count() == 0) {
        return;
    }

    // Leggi eventi dal buffer timeline
    TimelineBuffer* timeline = _keyer->getTimelineBuffer();
    if (!timeline || timeline->available() == 0) {
        return;
    }

    // Buffer per leggere eventi (max 100 per volta)
    const size_t MAX_EVENTS = 100;
    TimelineEvent events[MAX_EVENTS];
    size_t count = timeline->read(events, MAX_EVENTS);

    if (count == 0) {
        return;
    }

    // Crea JSON con eventi
    JsonDocument doc;
    JsonArray eventsArray = doc["events"].to<JsonArray>();

    for (size_t i = 0; i < count; i++) {
        JsonObject evt = eventsArray.add<JsonObject>();
        evt["ts"] = events[i].timestamp_us;

        // Tipo evento
        const char* type_str = "UNKNOWN";
        switch (events[i].type) {
            case EVENT_DOT_PRESS:    type_str = "DOT_PRESS"; break;
            case EVENT_DOT_RELEASE:  type_str = "DOT_RELEASE"; break;
            case EVENT_DASH_PRESS:   type_str = "DASH_PRESS"; break;
            case EVENT_DASH_RELEASE: type_str = "DASH_RELEASE"; break;
            case EVENT_KEY_ON:       type_str = "KEY_ON"; break;
            case EVENT_KEY_OFF:      type_str = "KEY_OFF"; break;
        }
        evt["type"] = type_str;

        // Flags
        JsonArray flags = evt["flags"].to<JsonArray>();
        if (events[i].flags & FLAG_IAMBIC) {
            flags.add("IAMBIC");
        }
        if (events[i].flags & FLAG_MEMORY_LATCH) {
            flags.add("MEMORY_LATCH");
        }
        if (events[i].flags & FLAG_DEBOUNCE_SKIP) {
            flags.add("DEBOUNCE_SKIP");
        }
    }

    // Statistiche buffer
    JsonObject stats = doc["buffer_stats"].to<JsonObject>();
    stats["total_pushed"] = timeline->getTotalPushed();
    stats["total_dropped"] = timeline->getTotalDropped();
    stats["overruns"] = timeline->getOverruns();
    stats["available"] = timeline->available();

    // Serializza e invia
    String output;
    serializeJson(doc, output);
    _ws.textAll(output);
}

void WebServerManager::handle() {
    // AsyncWebServer gestisce tutto automaticamente, questo metodo è vuoto
    // Mantenuto per compatibilità futura
}
