#include <Arduino.h>
#include <Adafruit_MAX31865.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>
#include <time.h> // Ajout pour NTP

// --- CONFIGURATION MATÉRIELLE ---
// ATTENTION : Pour le Deep Sleep, relier D0 (GPIO16) à RST. 
// Le CS du MAX31865 doit donc bouger sur D4 (GPIO2).
#define MAX_CS D4 
#define DF_RX D2
#define DF_TX D1
#define RNOMINAL  100.0
#define RREF      430.0

// Variables globales
float tempFireActive = 80.0;
float tempAlarmThreshold = 80.0;
float tempStoveCold = 40.0;
float tempOffset = -5.0;
float currentTemp = 0.0;
bool friendlyMode = true; // Mode "Sympa" actif par défaut
bool loggingEnabled = true;
int volume = 30;
float tempHighAlert = 170.0;
int sleepStartHour = 23; // Heure de début sommeil (ex: 23h)
int sleepEndHour = 7;    // Heure de fin sommeil (ex: 7h)

enum StoveState { STOVE_OFF, STOVE_BURNING, STOVE_ALERT };
StoveState currentState = STOVE_OFF;

Adafruit_MAX31865 thermo = Adafruit_MAX31865(MAX_CS);
ESP8266WebServer server(80);
SoftwareSerial mp3Serial(DF_TX, DF_RX); // TX, RX
DFRobotDFPlayerMini myDFPlayer;

// --- FONCTIONS SONORES (DFPlayer) ---
void playMP3(int fileIndex) {
  myDFPlayer.playMp3Folder(fileIndex);
}

void playMelodyInit() { playMP3(4); } // Accueil
void playMelodyCold() { playMP3(3); } // Froid
void playAlertSignal() { playMP3(1); } // Alerte extinction



// --- LOGGING ---
void appendLog(float temp) {
  File f = LittleFS.open("/log.csv", "a");
  if (f) {
    if (f.size() > 60000) { // Rotation à ~60ko (env 12h). log.old + log.csv = 24h glissant
       f.close();
       LittleFS.remove("/log.old");
       LittleFS.rename("/log.csv", "/log.old");
       f = LittleFS.open("/log.csv", "w");
       f.println("Heure;Temp");
    }
    
    // Horodatage
    time_t now = time(nullptr);
    char timeStr[20];
    if (now > 1000) { // Si NTP synchronisé
      strftime(timeStr, sizeof(timeStr), "%H:%M:%S", localtime(&now));
    } else {
      sprintf(timeStr, "%lu", millis()/1000); // Fallback si pas de NTP
    }

    f.printf("%s;%.0f\n", timeStr, temp);
    f.close();
  }
}

// --- GESTION DES FICHIERS ---
void loadConfig() {
  if (LittleFS.begin()) {
    if (LittleFS.exists("/config.txt")) {
      File f = LittleFS.open("/config.txt", "r");
      if (f) {
        tempFireActive = f.readStringUntil('\n').toFloat();
        tempAlarmThreshold = f.readStringUntil('\n').toFloat();
        tempStoveCold = f.readStringUntil('\n').toFloat();
        tempOffset = f.readStringUntil('\n').toFloat();
        String fMode = f.readStringUntil('\n');
        friendlyMode = (fMode.startsWith("1"));
        
        String sHigh = f.readStringUntil('\n'); if(sHigh.length() > 0) tempHighAlert = sHigh.toFloat();
        String sVol = f.readStringUntil('\n'); if(sVol.length() > 0) volume = sVol.toInt();
        String sLog = f.readStringUntil('\n'); if(sLog.length() > 0) loggingEnabled = (sLog.startsWith("1"));
        String sSS = f.readStringUntil('\n'); if(sSS.length() > 0) sleepStartHour = sSS.toInt();
        String sSE = f.readStringUntil('\n'); if(sSE.length() > 0) sleepEndHour = sSE.toInt();

        f.close();
      }
    }
  }
}

void saveConfig() {
  File f = LittleFS.open("/config.txt", "w");
  if (f) {
    f.println(tempFireActive);
    f.println(tempAlarmThreshold);
    f.println(tempStoveCold);
    f.println(tempOffset);
    f.println(friendlyMode ? "1" : "0");
    f.println(tempHighAlert);
    f.println(volume);
    f.println(loggingEnabled ? "1" : "0");
    f.println(sleepStartHour);
    f.println(sleepEndHour);
    f.close();
  }
}

// --- WEB SERVER ---
// API pour fournir la configuration au front-end
void handleGetConfig() {
  String json = "{";
  json += "\"t1\":" + String(tempFireActive) + ",";
  json += "\"t2\":" + String(tempAlarmThreshold) + ",";
  json += "\"t3\":" + String(tempStoveCold) + ",";
  json += "\"off\":" + String(tempOffset) + ",";
  json += "\"tha\":" + String(tempHighAlert) + ",";
  json += "\"vol\":" + String(volume) + ",";
  json += "\"fmode\":" + String(friendlyMode ? "true" : "false") + ",";
  json += "\"log\":" + String(loggingEnabled ? "true" : "false") + ",";
  json += "\"ss\":" + String(sleepStartHour) + ",";
  json += "\"se\":" + String(sleepEndHour) + ",";
  json += "\"ssid\":\"" + WiFi.SSID() + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleData() {
  String statusText = "FROID";
  if(currentState == STOVE_BURNING) statusText = "EN CHAUFFE";
  if(currentState == STOVE_ALERT) statusText = "ALERTE !";
  
  time_t now = time(nullptr);
  char timeStr[10];
  if (now > 1000) strftime(timeStr, sizeof(timeStr), "%H:%M", localtime(&now));
  else strcpy(timeStr, "--:--");

  char json[128];
  snprintf(json, sizeof(json), "{\"temp\":\"%.0f\", \"status\":\"%s\", \"state\":%d, \"time\":\"%s\"}", 
           currentTemp, statusText.c_str(), (int)currentState, timeStr);

  server.send(200, "application/json", json);
}

void handleSave() {
  if (server.hasArg("t1")) tempFireActive = server.arg("t1").toFloat();
  if (server.hasArg("t2")) tempAlarmThreshold = server.arg("t2").toFloat();
  if (server.hasArg("t3")) tempStoveCold = server.arg("t3").toFloat();
  if (server.hasArg("off")) tempOffset = server.arg("off").toFloat();
  if (server.hasArg("tha")) tempHighAlert = server.arg("tha").toFloat();
  if (server.hasArg("vol")) { 
    volume = server.arg("vol").toInt(); 
    if(volume < 0) volume = 0; if(volume > 30) volume = 30;
    myDFPlayer.volume(volume); 
  }
  friendlyMode = server.hasArg("fmode");
  loggingEnabled = server.hasArg("log");
  if(server.hasArg("ss")) sleepStartHour = server.arg("ss").toInt();
  if(server.hasArg("se")) sleepEndHour = server.arg("se").toInt();
  saveConfig();
  server.send(200, "text/html", "<html><body><script>alert('Configuration enregistr\\u00E9e !'); window.location='/';</script></body></html>");
}

// Utilisé par le graphique (chart.html) pour récupérer l'historique concaténé
void handleDataChart() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");

  auto sendFile = [](String path) {
    if (LittleFS.exists(path)) {
      File f = LittleFS.open(path, "r");
      if (f) {
        uint8_t buf[512];
        while (f.available()) {
          int len = f.read(buf, sizeof(buf));
          server.sendContent((const char*)buf, len);
        }
        f.close();
      }
    }
  };

  sendFile("/log.old");
  sendFile("/log.csv");
  server.sendContent("");
}

// Ancienne fonction handleClearLog retirée car inutilisée


void setup() {
  Serial.begin(115200);
  mp3Serial.begin(9600);
  
  thermo.begin(MAX31865_2WIRE);
  loadConfig();

  // Initialisation propre sans debug verbeux
  if (myDFPlayer.begin(mp3Serial, false, true)) {
    myDFPlayer.volume(volume);
  }

  // WiFiManager
  WiFiManager wifiManager;
  // wifiManager.setDebugOutput(false);
  wifiManager.setConfigPortalTimeout(180); // 3 minutes pour configurer si besoin
  
  if (!wifiManager.autoConnect("FireWatcher-Setup", "12345678")) {
    Serial.println("Échec connexion, redémarrage...");
    delay(3000);
    ESP.restart();
  }
  
  // Configuration NTP (Heure France)
  configTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");

  // Servir l'interface statique
  server.serveStatic("/", LittleFS, "/index.html");
  server.serveStatic("/style.css", LittleFS, "/style.css"); 
  server.serveStatic("/chart", LittleFS, "/chart.html");
  server.on("/get_config", handleGetConfig); // API config
  server.on("/data", handleData);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/doc.csv", handleDataChart); // API pour le graphique
  // server.on("/clearlog", handleClearLog); // Retiré
 
  server.begin();

  // Initialisation OTA
  ArduinoOTA.setHostname("FireWatcher");
  ArduinoOTA.begin();

  delay(2000);
  playMelodyInit(); // Bonjour Olivier...
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  
  // --- GESTION DU SOMMEIL PROFOND (MODE NUIT) ---
  // On ne dort que si le poêle est FROID (STOVE_OFF) pour la sécurité
  time_t now = time(nullptr);
  if (now > 1600000000 && currentState == STOVE_OFF) { // Si l'heure est valide (NTP ok)
    struct tm * timeinfo = localtime(&now);
    int currentHour = timeinfo->tm_hour;

    // Logique simple pour passage minuit (ex: 23h à 7h)
    bool shouldSleep = false;
    if (sleepStartHour > sleepEndHour) {
      if (currentHour >= sleepStartHour || currentHour < sleepEndHour) shouldSleep = true;
    } else {
       // Cas rare : ex 01h à 05h
      if (currentHour >= sleepStartHour && currentHour < sleepEndHour) shouldSleep = true;
    }

    if (shouldSleep) {
      // Calcul du temps de sommeil en secondes jusqu'à l'heure de réveil
      int secondsUntilWakeup = 0;
      if (currentHour >= sleepStartHour) {
        // Avant minuit
        secondsUntilWakeup = ((24 - currentHour) + sleepEndHour) * 3600 - (timeinfo->tm_min * 60) - timeinfo->tm_sec;
      } else {
        // Après minuit
        secondsUntilWakeup = (sleepEndHour - currentHour) * 3600 - (timeinfo->tm_min * 60) - timeinfo->tm_sec;
      }
      
      // Sécurité: Si calcul foireux ou trop court, on ne dors pas tout de suite
      if (secondsUntilWakeup > 60) {
        Serial.printf("Activating Deep Sleep for %d seconds\n", secondsUntilWakeup);
        // Deep Sleep max ~3h sur ESP8266. On limite.
        // Si wakeup > 3h, on dort 3h, on se réveille, on reconnecte et on redort.
        uint64_t sleepUs = (uint64_t)secondsUntilWakeup * 1000000ULL;
        uint64_t maxSleep = 10000000000ULL; // ~2.7h (safe margin)
        if (sleepUs > maxSleep) sleepUs = maxSleep;
        
        ESP.deepSleep(sleepUs);
      }
    }
  }

  static unsigned long lastUpdate = 0;
  static unsigned long lastFriendlyMsg = 0;
  static unsigned long lastBip = 0;

  if (millis() - lastUpdate > 5000) { 
    lastUpdate = millis();
    
    // Log périodique (toutes les 30s = 6 cycles de 5s)
    static int logCounter = 0;
    if (++logCounter >= 6) {
       logCounter = 0;
       if (loggingEnabled) appendLog(currentTemp);
    }
    
    float rawTemp = thermo.temperature(RNOMINAL, RREF);
    currentTemp = rawTemp + tempOffset;
    uint8_t fault = thermo.readFault();

    // Alerte surchauffe
    static unsigned long lastHighTempAlert = 0;
    if (currentTemp > tempHighAlert) {
      if (millis() - lastHighTempAlert > 60000) {
        lastHighTempAlert = millis();
        playMP3(9);
      }
    }

    // Gestion des phrases d'agrément (Mode Sympathique)
    if (friendlyMode && currentState == STOVE_BURNING) {
      if (lastFriendlyMsg == 0) lastFriendlyMsg = millis(); // Init au premier démarrage
      if (millis() - lastFriendlyMsg > 1800000) { // Toutes les 30 minutes
        lastFriendlyMsg = millis();
        playMP3(random(5, 9)); // Phrases de 5 à 8
      }
    }

    switch (currentState) {
      case STOVE_OFF:
        if (currentTemp > tempFireActive) {
          currentState = STOVE_BURNING;
          playMP3(2); // Feu démarré
          lastFriendlyMsg = millis();
        }
        break;
      case STOVE_BURNING:
        if (currentTemp < tempAlarmThreshold) {
          currentState = STOVE_ALERT;
          playAlertSignal();
          lastBip = millis();
        }
        break;
      case STOVE_ALERT:
        if (millis() - lastBip > 60000) { 
          lastBip = millis();
          playAlertSignal();
          playAlertSignal();
        }
        // Hysteresis de sortie d'alerte : on sort si on repasse au dessus du seuil d'alerte + 2°C
        if (currentTemp > (tempAlarmThreshold + 2.0)) currentState = STOVE_BURNING;
        else if (currentTemp < tempStoveCold) {
          currentState = STOVE_OFF;
          playMelodyCold();
        }
        break;
    }
    if (fault) { thermo.clearFault(); }
  }
}
