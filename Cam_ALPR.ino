#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <DNSServer.h>
#include <esp_camera.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>

#define LED_FADE_SPEED 1   // Vitesse du fade (en unités de 0 à 255)
#define LED_FADE_MAX 5

volatile bool fadeInProgress = false;
TaskHandle_t fadeTaskHandle = NULL;
char* server_url="http://192.168.192.234:36700";

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer server(80);

const int configButton = 0; // Bouton de configuration sur GPIO 0

// Configuration de la caméra
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define LED_GPIO_NUM       4

struct Config {
  char ssid[32];
  char password[64];
  char server_url[128];
  char license_plate_db_url[128];
  char ap_ssid[32];
  char ap_password[64];
  char cam_id[16];
  int flash_state;
};

Config config;

void setup() {
  Serial.begin(115200);
  EEPROM.begin(sizeof(Config));

  // Chargement des données de configuration depuis l'EEPROM
  EEPROM.get(0, config);
  
  // Configurer la LED comme sortie
  pinMode(LED_GPIO_NUM, OUTPUT);
  digitalWrite(LED_GPIO_NUM, LOW); // Assurer que la LED est éteinte au démarrage
  
  // Configurer la caméra
  camera_config_t camera_config;
  camera_config.ledc_channel = LEDC_CHANNEL_0;
  camera_config.ledc_timer = LEDC_TIMER_0;
  camera_config.pin_d0 = Y2_GPIO_NUM;
  camera_config.pin_d1 = Y3_GPIO_NUM;
  camera_config.pin_d2 = Y4_GPIO_NUM;
  camera_config.pin_d3 = Y5_GPIO_NUM;
  camera_config.pin_d4 = Y6_GPIO_NUM;
  camera_config.pin_d5 = Y7_GPIO_NUM;
  camera_config.pin_d6 = Y8_GPIO_NUM;
  camera_config.pin_d7 = Y9_GPIO_NUM;
  camera_config.pin_xclk = XCLK_GPIO_NUM;
  camera_config.pin_pclk = PCLK_GPIO_NUM;
  camera_config.pin_vsync = VSYNC_GPIO_NUM;
  camera_config.pin_href = HREF_GPIO_NUM;
  camera_config.pin_sscb_sda = SIOD_GPIO_NUM;
  camera_config.pin_sscb_scl = SIOC_GPIO_NUM;
  camera_config.pin_pwdn = PWDN_GPIO_NUM;
  camera_config.pin_reset = RESET_GPIO_NUM;
  camera_config.xclk_freq_hz = 20000000;
  camera_config.pixel_format = PIXFORMAT_JPEG;

  if(psramFound()){
    camera_config.frame_size = FRAMESIZE_QVGA;
    camera_config.jpeg_quality = 30;
    camera_config.fb_count = 2;
  } else {
    camera_config.frame_size = FRAMESIZE_SVGA;
    camera_config.jpeg_quality = 12;
    camera_config.fb_count = 1;
  }

  // Initialiser la caméra
  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  
  else {
    Serial.println("\n\nESP32-Cam initialisé.\n");
  }
  

  // Tenter de se connecter au réseau configuré
  Serial.print("Connection au réseau [" + String(config.ssid) + "] avec le mot de passe [" + String(config.password) + "]...");
  WiFi.begin(config.ssid, config.password);
  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    Serial.print(".");
    delay(1000);
    attempts++;
  }

  if (attempts < 10){
    Serial.println("[Succès]\n\nConnecté au réseau [" + String(config.ssid) + "].");
    while (!testALPRServer()){
      Serial.println("Serveur ALPR hors-ligne.");
      delay(5000);
    }
  }
  else {
    Serial.println("[Échec]\n\nNombre de tentatives dépassé. Démarrage du point d'accès de configuration.\n");
    startConfigMode();
  }
}

void loop() {
  if (WiFi.getMode() == WIFI_AP) {
    dnsServer.processNextRequest();
    server.handleClient();
  }
  else{
    // Exécution du code de prise de photos :
    captureAndSendPhoto(config.flash_state);
    delay(5000);
  }
}

void startConfigMode() {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(config.ap_ssid, config.ap_password);

  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  });

  server.begin();
  Serial.println("Point d'accès de configuration démarré.\n  ↪ nom : [" + String(config.ap_ssid) + "] - mot de passe : [" + String(config.ap_password) + "].");
  fade_LED(true);
}

void handleRoot() {
  String html = "<html>";
  html += "<head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"; // Optimisation pour mobile
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; font-size: 18px; }"; // Texte lisible
  html += "h1 { font-size: 24px; }";  // Taille du titre principale
  html += "h2 { font-size: 20px; margin-top: 20px; }";  // Taille des sous-titres
  html += "input[type='text'], input[type='password'] { width: 100%; padding: 8px; margin: 8px 0; box-sizing: border-box; font-size: 16px; }";  // Champs de texte adaptés à la largeur
  html += "input[type='submit'] { background-color: #4CAF50; color: white; padding: 10px 20px; border: none; cursor: pointer; font-size: 16px; }";  // Style du bouton
  html += "</style>";
  html += "</head>";
  html += "<body>";

  html += "<h1>Configuration ESP32-Cam</h1>";
  html += "<form action='/save' method='post'>";

  // Section Réseau auquel se connecter
  html += "<h2>Réseau auquel se connecter</h2>";
  html += "SSID: <input type='text' name='ssid' value='" + String(config.ssid) + "'><br>";
  html += "Mot de passe: <input type='text' name='password' value='" + String(config.password) + "'><br>";

  // Section Adresses de traitement des images
  html += "<h2>Adresses de traitement des images</h2>";
  html += "Adresse du serveur d'ALPR: <input type='text' name='server_url' value='" + String(config.server_url) + "'><br>";
  html += "Adresse de la base de données de plaques: <input type='text' name='license_plate_db_url' value='" + String(config.license_plate_db_url) + "'><br>";

  // Section Paramètres du point d'accès
  html += "<h2>Paramètres du point d'accès</h2>";
  html += "Nom du point d'accès (AP): <input type='text' name='ap_ssid' value='" + String(config.ap_ssid) + "'><br>";
  html += "Mot de passe du point d'accès (AP): <input type='text' name='ap_password'value='" + String(config.ap_password) + "'><br>";

  // Section Paramètres de la caméra
  html += "<h2>Paramètres de la caméra</h2>";
  html += "Flash (0-255): <input type='number' name='flash_state' value='" + String(config.flash_state) + "'><br>";
  html += "ID de la caméra: <input type='text' name='cam_id' value='" + String(config.cam_id) + "'><br>";

  html += "<input type='submit' value='Enregistrer'>";
  html += "</form>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  // Sauvegarde des paramètres réseaux
  strlcpy(config.ssid, server.arg("ssid").c_str(), sizeof(config.ssid));
  strlcpy(config.password, server.arg("password").c_str(), sizeof(config.password));
  strlcpy(config.server_url, server.arg("server_url").c_str(), sizeof(config.server_url));
  strlcpy(config.license_plate_db_url, server.arg("license_plate_db_url").c_str(), sizeof(config.license_plate_db_url));
  strlcpy(config.ap_ssid, server.arg("ap_ssid").c_str(), sizeof(config.ap_ssid));
  strlcpy(config.ap_password, server.arg("ap_password").c_str(), sizeof(config.ap_password));

  // Conversion des données numériques
  config.flash_state = server.arg("flash_state").toInt();  // Utiliser toInt() pour convertir en entier
  strlcpy(config.cam_id, server.arg("cam_id").c_str(), sizeof(config.cam_id));  // ID caméra

  // Sauvegarder la configuration dans l'EEPROM
  EEPROM.put(0, config);
  EEPROM.commit();

  // Retour de la réponse HTML
  String html = "<html>";
  html += "<head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; font-size: 18px; background-color: #f4f4f9; }";
  html += ".message { padding: 20px; background-color: #d4edda; color: #155724; border: 2px solid #c3e6cb; border-radius: 5px; margin-top: 50px; font-size: 18px; text-align: center; }";
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<div class='message'>Configuration sauvegardée. L'ESP32 va redémarrer. Vous pouvez fermer cette page.</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);

  fade_LED(false);
  ESP.restart();  // Redémarrage de l'ESP32
}

void fadeLED(void *parameter) {
    int stepDelay = 150/LED_FADE_MAX;  // Calculer le délai entre les étapes
    while (true) {
        if (fadeInProgress) {
            // Fondu entrant
            for (int i = 0; i <= LED_FADE_MAX; i += LED_FADE_SPEED) {
                analogWrite(LED_GPIO_NUM, i); // Écrire la valeur de luminosité
                delay(stepDelay); // Délai pour l'effet de fondu
            }
            // Fondu sortant
            for (int i = LED_FADE_MAX; i >= 0; i -= LED_FADE_SPEED) {
                analogWrite(LED_GPIO_NUM, i); // Écrire la valeur de luminosité
                delay(stepDelay); // Délai pour l'effet de fondu
            }
            delay(800);
        } else {
            // Si le fondu n'est pas en cours, on attend
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }
}

void fade_LED(bool start) {
    if (start) {
        if (!fadeInProgress) {
            fadeInProgress = true;
            if (fadeTaskHandle == NULL) {
                xTaskCreate(fadeLED, "FadeLEDTask", 2048, NULL, 1, &fadeTaskHandle);
            }
        }
    } else {
        fadeInProgress = false;
        if (fadeTaskHandle != NULL) {
            vTaskDelete(fadeTaskHandle);
            fadeTaskHandle = NULL;
            analogWrite(LED_GPIO_NUM, 0); // Éteindre la LED à la fin du fade
        }
    }
}


void captureAndSendPhoto(int flash_value) {
  // Configurer le flash
  analogWrite(LED_GPIO_NUM, flash_value);
  delay(20);

  // Capture de la photo
  camera_fb_t *fb = esp_camera_fb_get();

  analogWrite(LED_GPIO_NUM, 0);  // Éteindre le flash

  if (!fb) {
    Serial.println("Échec de la capture de la photo");
    ESP.restart();
    return;
  }

  Serial.printf("Photo capturée. Taille: %zu bytes\n", fb->len);


  const char* content_start_text = "------boundary------Content-Disposition: form-data; name=\"image\"; filename=\"image.jpg\"Content-Type: image/jpeg";
  char* content_end_text = "------boundary------";

  
  const char * content_start = "------boundary------Content-Disposition: form-data; name=\"image\"; filename=\"image.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n\\";
  const char * content_end = "\r\n------boundary------\r\n";

  size_t content_start_lenght = strlen(content_start_text)+6;
  size_t content_end_lenght = strlen(content_end_text)+4;

  uint8_t* buff = (uint8_t*)malloc((fb->len+content_start_lenght+content_end_lenght) * sizeof(uint8_t));
  size_t buffSize = 0;

  memcpy(buff, content_start, content_start_lenght);
  buffSize += content_start_lenght;

  memcpy(buff + buffSize, fb->buf, fb->len);
  buffSize += fb->len;

  memcpy(buff + buffSize, content_end, content_end_lenght);
  buffSize += content_end_lenght;
  // Préparer la requête HTTP
  HTTPClient http;

  http.begin(config.server_url);
  http.addHeader("Content-Type", "multipart/form-data; boundary="+String(content_end_text));
  http.addHeader("Content-Length", String(buffSize));

  if (!fb || fb->len == 0) {
  Serial.println("Échec de la capture de la photo ou image vide");
  }

  uint8_t httpResponseCode = http.POST(buff, buffSize);
  free(buff);
  // Vérifier la réponse
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Réponse de l'API :");

    // Parser la réponse JSON
    DynamicJsonDocument doc(1024); // Taille du document JSON à adapter si nécessaire
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      Serial.print("Échec du parsing JSON : ");
      Serial.println(error.c_str());
    } else {
      // Sérialiser le JSON avec indentation pour affichage
      String formattedResponse;
      serializeJsonPretty(doc, formattedResponse);
      Serial.println(formattedResponse);

      // Vérifier le succès de l'analyse
      if (doc["success"].as<bool>() == false) {
        Serial.println("L'API n'a pas pu analyser l'image. Raison : " + doc["error"].as<String>());
      }
    }
  }
  
  else {
    Serial.printf("Erreur de requête HTTP : %d\n", httpResponseCode);
  }

  // Libérer les ressources
  esp_camera_fb_return(fb);
  http.end();
}


bool testALPRServer() {
  WiFiClient client;
  HTTPClient http;

  Serial.println("Test de connexion au serveur ALPR:" + String(config.server_url));

  // Configurer la requête HTTP
  http.begin(client, config.server_url);

  // Envoyer une requête GET de test
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    Serial.printf("Réponse du serveur ALPR. Code: %d\n", httpResponseCode);
    String payload = http.getString();
    Serial.println("Réponse: " + payload);
    http.end();
    return true;
  }
  else {
    Serial.printf("Erreur de connexion au serveur ALPR. Code d'erreur: %d\n", httpResponseCode);
    http.end();
    return true;
  }
}