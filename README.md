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
  - Réglage du volume sonore et du mode "Sympathique" (messages vocaux aléatoires).
- **Audio** : Feedback sonore via DFPlayer Mini.

## Mise à jour en OTA (Over-The-Air)

Le firmware est configuré pour être mis à jour via le réseau WiFi sans brancher le câble USB.

### Prérequis
- L'ordinateur et le module FireWatcher doivent être sur le même réseau WiFi.
- PlatformIO doit être installé.

### Configuration
Dans le fichier `platformio.ini`, assurez-vous que les lignes suivantes sont actives (l'adresse IP doit correspondre à celle de votre module, visible sur l'interface web ou via un scan réseau) :

```ini
upload_protocol = espota
upload_port = 192.168.1.21  ; Remplacez par l'IP de votre module
```

### Commande de mise à jour
Ouvrez un terminal dans le dossier du projet et lancez :

```bash
platformio run --target upload
```

Si tout se passe bien, la compilation se lance, suivie du téléversement via le réseau.
