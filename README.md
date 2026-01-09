# FireWatcher

FireWatcher est un système de surveillance pour poêle à bois basé sur un ESP8266 (Wemos D1 Mini) et un capteur de température PT100 (via un amplificateur MAX31865).

## Fonctionnalités

- **Surveillance de Température** : Mesure la température du conduit de cheminée.
- **Alertes** : 
  - Détection de feu éteint (température descend sous un seuil après avoir été chaud).
  - Alerte de surchauffe (température dépasse un seuil critique).
- **Interface Web** :
  - Visualisation en temps réel de la température et de l'état.
  - Configuration des seuils (feu actif, alerte froid, surchauffe).
  - Réglage du volume sonore et du mode "Sympa" (messages vocaux aléatoires).
- **Audio** : Feedback sonore via DFPlayer Mini.
- **Logging** : Enregistrement de la température et de l'état toutes les 30 secondes dans un fichier CSV téléchargeable.

## Mise à jour en OTA (Over-The-Air)

Le firmware est configuré pour être mis à jour via le réseau WiFi sans brancher le câble USB.

### Prérequis
- L'ordinateur et le module FireWatcher doivent être sur le même réseau WiFi.
- PlatformIO doit être installé.

### Configuration
Dans le fichier `platformio.ini`, assurez-vous que les lignes suivantes sont actives (l'adresse IP doit correspondre à celle de votre module) :

```ini
upload_protocol = espota
upload_port = 192.168.1.21  ; Remplacez par l'IP de votre module
```

### Commandes de mise à jour

**1. Mise à jour du Firmware (Logiciel)**
À faire lors de modifications du code C++ (`src/main.cpp`).
```bash
platformio run --target upload
```

**2. Mise à jour du Système de Fichiers (Interface Web)**
À faire obligatoirement lors de la première installation ou si vous modifiez le fichier `data/index.html`.
```bash
platformio run --target uploadfs
```
*Note : Si l'interface web ne s'affiche pas ou tourne dans le vide, c'est souvent parce que cette étape a été oubliée.*

