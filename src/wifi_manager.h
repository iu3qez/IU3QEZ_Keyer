#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "settings.h"

enum WiFiMgrMode {
    WIFIMGR_MODE_AP = 0,
    WIFIMGR_MODE_STA = 1
};

class WiFiManager {
public:
    WiFiManager();

    // Inizializza WiFi (modalità da settings.h)
    bool begin();

    // Ottieni informazioni
    String getSSID() { return _ssid; }
    IPAddress getIP() { return _ip; }
    WiFiMgrMode getMode() { return _mode; }
    uint8_t getNumClients();  // Solo per modalità AP
    bool isConnected();       // Solo per modalità STA

    // Debug
    void printStatus();

private:
    WiFiMgrMode _mode;
    String _ssid;
    String _password;
    IPAddress _ip;
    uint8_t _channel;

    // Metodi interni
    bool beginAP();
    bool beginSTA();
};

#endif // WIFI_MANAGER_H
