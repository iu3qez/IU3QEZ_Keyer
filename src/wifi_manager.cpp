#include "wifi_manager.h"

WiFiManager::WiFiManager() {
    // Configurazione iniziale da settings.h
#if KEYER_WIFI_MODE == KEYER_WIFI_MODE_AP
    _mode = WIFIMGR_MODE_AP;
    _ssid = WIFI_AP_SSID;
    _password = WIFI_AP_PASSWORD;
    _channel = WIFI_AP_CHANNEL;
#else
    _mode = WIFIMGR_MODE_STA;
    _ssid = WIFI_STA_SSID;
    _password = WIFI_STA_PASSWORD;
    _channel = 0;
#endif
}

bool WiFiManager::begin() {
    Serial.println("\n=== WiFi Setup ===");

    // Disabilita WiFi esistente
    WiFi.mode(WIFI_OFF);
    delay(100);

    // Avvia modalità selezionata
    if (_mode == WIFIMGR_MODE_AP) {
        return beginAP();
    } else {
        bool success = beginSTA();

#if WIFI_STA_FALLBACK_TO_AP
        // Fallback ad AP se STA fallisce
        if (!success) {
            Serial.println("FALLBACK: Avvio Access Point...");
            _mode = WIFIMGR_MODE_AP;
            _ssid = WIFI_AP_SSID;
            _password = WIFI_AP_PASSWORD;
            _channel = WIFI_AP_CHANNEL;
            return beginAP();
        }
#endif
        return success;
    }
}

bool WiFiManager::beginAP() {
    Serial.println("Modalità: Access Point");

    // Configura come Access Point
    WiFi.mode(WIFI_AP);
    delay(100);

    // Avvia AP
    bool success = WiFi.softAP(_ssid.c_str(), _password.c_str(), _channel, 0, WIFI_AP_MAX_CONN);

    if (!success) {
        Serial.println("ERRORE: Impossibile avviare WiFi AP!");
        return false;
    }

    delay(500);  // Attendi stabilizzazione

    // Ottieni IP (default 192.168.4.1)
    _ip = WiFi.softAPIP();

    Serial.printf("WiFi AP avviato con successo!\n");
    Serial.printf("  SSID: %s\n", _ssid.c_str());
    Serial.printf("  Password: %s\n", _password.c_str());
    Serial.printf("  IP: %s\n", _ip.toString().c_str());
    Serial.printf("  Channel: %d\n", _channel);
    Serial.printf("  Max Clients: %d\n", WIFI_AP_MAX_CONN);

    return true;
}

bool WiFiManager::beginSTA() {
    Serial.println("Modalità: Station (client)");
    Serial.printf("Connessione a: %s\n", _ssid.c_str());

    // Configura come Station
    WiFi.mode(WIFI_STA);
    delay(100);

    // Connetti all'AP
    WiFi.begin(_ssid.c_str(), _password.c_str());

    // Attendi connessione con timeout
    uint32_t start = millis();
    Serial.print("Connessione in corso");

    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_STA_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("ERRORE: Timeout connessione WiFi!");
        return false;
    }

    // Connesso! Ottieni IP via DHCP
    _ip = WiFi.localIP();

    Serial.printf("WiFi Station connesso con successo!\n");
    Serial.printf("  SSID: %s\n", _ssid.c_str());
    Serial.printf("  IP: %s\n", _ip.toString().c_str());
    Serial.printf("  Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("  DNS: %s\n", WiFi.dnsIP().toString().c_str());
    Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());

    return true;
}

uint8_t WiFiManager::getNumClients() {
    if (_mode == WIFIMGR_MODE_AP) {
        return WiFi.softAPgetStationNum();
    }
    return 0;
}

bool WiFiManager::isConnected() {
    if (_mode == WIFIMGR_MODE_STA) {
        return WiFi.status() == WL_CONNECTED;
    }
    return true;  // AP è sempre "connesso"
}

void WiFiManager::printStatus() {
    if (_mode == WIFIMGR_MODE_AP) {
        Serial.printf("WiFi AP Status:\n");
        Serial.printf("  SSID: %s\n", _ssid.c_str());
        Serial.printf("  IP: %s\n", _ip.toString().c_str());
        Serial.printf("  Connected Clients: %d\n", getNumClients());
    } else {
        Serial.printf("WiFi STA Status:\n");
        Serial.printf("  SSID: %s\n", _ssid.c_str());
        Serial.printf("  IP: %s\n", _ip.toString().c_str());
        Serial.printf("  Connected: %s\n", isConnected() ? "Yes" : "No");
        if (isConnected()) {
            Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
        }
    }
}
