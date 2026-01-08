#include <Arduino.h>
#include <Adafruit_MAX31865.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>

// --- CONFIGURATION MATÉRIELLE ---
#define MAX_CS D0
#define DF_RX D2
#define DF_TX D1
#define RNOMINAL  100.0
#define RREF      430.0

// Variables globales
float tempFireActive = 80.0;
float tempAlarmThreshold = 60.0;
float tempStoveCold = 40.0;
float tempOffset = -5.0;
float currentTemp = 0.0;
bool friendlyMode = true; // Mode "Sympathique" actif par défaut
int volume = 30;
float tempHighAlert = 150.0;

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
    f.close();
  }
}

// --- WEB SERVER ---
void handleRoot() {
  String color = "#3498db";
  String statusText = "FROID";
  if(currentState == STOVE_BURNING) { color = "#27ae60"; statusText = "EN CHAUFFE"; }
  if(currentState == STOVE_ALERT) { color = "#e67e22"; statusText = "ALERTE !"; }

  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>FireWatcher</title><style>";
  html += "body{font-family:sans-serif; background:#f4f4f4; text-align:center; padding:20px;}";
  html += ".card{background:white; padding:25px; border-radius:15px; box-shadow:0 10px 20px rgba(0,0,0,0.1); max-width:400px; margin:auto;}";
  html += ".temp{font-size:3.5em; font-weight:bold; color:#2c3e50; margin:15px 0;}";
  html += ".status{display:inline-block; padding:8px 20px; border-radius:20px; color:white; font-weight:bold; background:" + color + ";}";
  html += "form{margin-top:20px; text-align:left;} label{display:block; margin-top:10px; font-weight:bold; font-size:0.9em;}";
  html += "input[type='number']{width:100%; padding:10px; margin:5px 0; border:1px solid #ddd; border-radius:5px;}";
  html += ".switch{margin:15px 0; display:flex; align-items:center; gap:10px;}";
  html += "button{background:#2c3e50; color:white; border:none; padding:15px; width:100%; border-radius:5px; cursor:pointer; font-size:1.1em; margin-top:20px;}";
  html += ".info{margin-top:10px; font-size:0.8em; color:#7f8c8d;}";
  html += "</style></head><body>";
  html += "<div class='card'><h1>🔥 FireWatcher</h1>";
  html += "<div id='status_box' class='status'>" + statusText + "</div>";
  html += "<div id='temp_display' class='temp'>" + String(currentTemp, 1) + "&deg;C</div>";
  html += "<form action='/save' method='POST'>";
  html += "<label>Seuil Feu Actif (&deg;C)</label><input type='number' step='0.1' name='t1' value='" + String(tempFireActive) + "'>";
  html += "<label>Alerte si inf. à (&deg;C)</label><input type='number' step='0.1' name='t2' value='" + String(tempAlarmThreshold) + "'>";
  html += "<label>Seuil Poêle Froid (&deg;C)</label><input type='number' step='0.1' name='t3' value='" + String(tempStoveCold) + "'>";
  html += "<label>Calibration Offset (&deg;C)</label><input type='number' step='0.1' name='off' value='" + String(tempOffset) + "'>";
  html += "<label>Seuil Surchauffe (&deg;C)</label><input type='number' step='0.1' name='tha' value='" + String(tempHighAlert) + "'>";
  html += "<label>Volume (0-30)</label><input type='number' name='vol' value='" + String(volume) + "'>";
  html += "<div class='switch'><input type='checkbox' name='fmode' id='fmode' " + String(friendlyMode ? "checked" : "") + "> <label for='fmode'>Mode Sympa</label></div>";
  html += "<button type='submit'>Enregistrer</button></form>";
  html += "<div class='info'>Connecté à : " + WiFi.SSID() + "<br>IP : " + WiFi.localIP().toString() + "</div></div>";
  
  // Script AJAX pour rafraîchir uniquement la température sans recharger la page
  html += "<script>";
  html += "setInterval(function(){";
  html += " fetch('/data').then(r => r.json()).then(data => {";
  html += "  document.getElementById('temp_display').innerHTML = data.temp + '&deg;C';";
  html += "  let s = document.getElementById('status_box'); s.innerHTML = data.status;";
  html += "  s.style.background = (data.state == 1 ? '#27ae60' : (data.state == 2 ? '#e67e22' : '#3498db'));";
  html += " });";
  html += "}, 5000);";
  html += "</script></body></html>";
  server.send(200, "text/html", html);
}

void handleData() {
  String statusText = "FROID";
  if(currentState == STOVE_BURNING) statusText = "EN CHAUFFE";
  if(currentState == STOVE_ALERT) statusText = "ALERTE !";
  
  String json = "{\"temp\":\"" + String(currentTemp, 1) + "\", \"status\":\"" + statusText + "\", \"state\":" + String(currentState) + "}";
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
  saveConfig();
  server.send(200, "text/html", "<html><body><script>alert('Configuration enregistr\\u00E9e !'); window.location='/';</script></body></html>");
}

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

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleRoot); 
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
  
  static unsigned long lastUpdate = 0;
  static unsigned long lastFriendlyMsg = 0;
  static unsigned long lastBip = 0;

  if (millis() - lastUpdate > 5000) { 
    lastUpdate = millis();
    
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
        }
        if (currentTemp > tempFireActive) currentState = STOVE_BURNING;
        else if (currentTemp < tempStoveCold) {
          currentState = STOVE_OFF;
          playMelodyCold();
        }
        break;
    }
    if (fault) { thermo.clearFault(); }
  }
}
