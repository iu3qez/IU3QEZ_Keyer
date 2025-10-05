#include "web_server.h"
#include "keyer_logic.h"
#include "sidetone_generator.h"
#include "config_manager.h"
#include <ArduinoJson.h>

WebServerManager::WebServerManager(KeyerLogic* keyer, SidetoneGenerator* sidetone, ConfigManager* configMgr)
    : _server(80), _keyer(keyer), _sidetone(sidetone), _configMgr(configMgr) {
}

bool WebServerManager::begin() {
    Serial.println("\n=== Web Server Setup ===");

    // Inizializza LittleFS per file statici
    if (!LittleFS.begin(true)) {  // true = format if mount fails
        Serial.println("ERRORE: LittleFS mount fallito!");
        return false;
    }
    Serial.println("LittleFS mounted");

    // Setup routes
    setupRoutes();

    // Avvia server
    _server.begin();
    Serial.println("Web Server avviato su porta 80");

    return true;
}

void WebServerManager::setupRoutes() {
    // Pagina principale (serviremo HTML statico da LittleFS)
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            // Fallback: pagina di benvenuto semplice
            String html = "<!DOCTYPE html><html><head><title>IU3QEZ Keyer</title></head>";
            html += "<body><h1>IU3QEZ CW HST Keyer</h1>";
            html += "<p>Web interface in development...</p>";
            html += "<p>API endpoints:</p><ul>";
            html += "<li><a href='/api/status'>/api/status</a></li>";
            html += "<li><a href='/api/config'>/api/config</a></li>";
            html += "</ul></body></html>";
            request->send(200, "text/html", html);
        }
    });

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

    // Serve static files from LittleFS
    _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

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

void WebServerManager::handle() {
    // AsyncWebServer gestisce tutto automaticamente, questo metodo è vuoto
    // Mantenuto per compatibilità futura
}
