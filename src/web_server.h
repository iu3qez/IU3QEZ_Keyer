#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>

// Forward declarations
class KeyerLogic;
class SidetoneGenerator;
class ConfigManager;

class WebServerManager {
public:
    WebServerManager(KeyerLogic* keyer, SidetoneGenerator* sidetone, ConfigManager* configMgr);

    // Inizializzazione
    bool begin();

    // Gestione server
    void handle();  // Chiamato da loop (opzionale con AsyncWebServer)

private:
    AsyncWebServer _server;
    KeyerLogic* _keyer;
    SidetoneGenerator* _sidetone;
    ConfigManager* _configMgr;

    // Setup routes
    void setupRoutes();

    // Handler API
    void handleGetConfig(AsyncWebServerRequest *request);
    void handlePostConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    void handleSaveConfig(AsyncWebServerRequest *request);
    void handleResetConfig(AsyncWebServerRequest *request);
    void handleGetStatus(AsyncWebServerRequest *request);
};

#endif // WEB_SERVER_H
