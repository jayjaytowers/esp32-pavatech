#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <time.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TM1637Display.h>

// Configuración WiFi
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL 6 // Defining the WiFi channel speeds up the connection

const uint8_t SEG_HOLA [] = {
	SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,           // H
	SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,   // O
	SEG_D | SEG_E | SEG_F,                           // L
	SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G    // A
};

// Configuración NTP
const char* ntpServer = "ar.pool.ntp.org";
const long gmtOffset_sec = -10800; // GMT-3 para Argentina
const int daylightOffset_sec = 0;

// Pines
#define BUTTON_PIN 12
#define BUZZER_PIN 14
#define RELAY_PIN 15
#define DS18B20_PIN 13
#define TM1637_CLK 22
#define TM1637_DIO 23

// Temperaturas preestablecidas
enum TempMode {
  MODE_CLOCK,
  MODE_TE,      // 70°C
  MODE_CAFE,    // 90°C
  MODE_MATE,    // 80°C
  MODE_HERVIR   // 100°C
};

const int TEMP_TE = 70;
const int TEMP_CAFE = 90;
const int TEMP_MATE = 80;
const int TEMP_HERVIR = 100;

// Variables globales
TempMode currentMode = MODE_CLOCK;
TempMode selectedMode = MODE_TE;
bool isHeating = false;
float currentTemp = 0.0;
int targetTemp = 0;
unsigned long buttonPressTime = 0;
bool buttonPressed = false;
bool longPressDetected = false;
int animationStep = 0;
unsigned long lastAnimationTime = 0;

// Objetos
OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);
TM1637Display display(TM1637_CLK, TM1637_DIO);
AsyncWebServer server(80);

// Notas musicales
#define NOTE_C5 523
#define NOTE_D5 587
#define NOTE_E5 659
#define NOTE_G5 784
#define NOTE_A5 880
#define NOTE_C6 1047

// Prototipos
void playStartupMelody();
void playBeep();
void playCompleteMelody();
void updateDisplay();
void showAnimation();
void handleButton();
void setupWebServer();
String getModeName(TempMode mode);

void setup() {
  Serial.begin(115200);
  
  // Configurar pines
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  
  // Inicializar display
  display.setBrightness(7);
  display.showNumberDec(0, false);
  
  // Inicializar sensor de temperatura
  sensors.begin();
  
  // Conectar WiFi
  WiFi.begin (WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  display.setSegments (SEG_HOLA); // Mostrar durante conexión
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  // Configurar NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Configurar servidor web
  setupWebServer();
  server.begin();
  
  // Melodía de inicio
  playStartupMelody();
}

void loop() {
  handleButton();
  
  // Leer temperatura cada 500ms
  static unsigned long lastTempRead = 0;
  if (millis() - lastTempRead > 500) {
    sensors.requestTemperatures();
    currentTemp = sensors.getTempCByIndex(0);
    lastTempRead = millis();
  }
  
  // Control de calentamiento
  if (isHeating) {
    if (currentTemp >= targetTemp) {
      digitalWrite(RELAY_PIN, LOW);
      isHeating = false;
      playCompleteMelody();
      currentMode = MODE_CLOCK;
    }
    // Mostrar animación mientras calienta
    if (millis() - lastAnimationTime > 300) {
      showAnimation();
      lastAnimationTime = millis();
    }
  } else {
    // Actualizar display cada 200ms cuando no está calentando
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 200) {
      updateDisplay();
      lastDisplayUpdate = millis();
    }
  }
  
  delay(10);
}

void handleButton() {
  bool buttonState = digitalRead(BUTTON_PIN) == LOW;
  
  if (buttonState && !buttonPressed) {
    // Botón presionado
    buttonPressed = true;
    buttonPressTime = millis();
    longPressDetected = false;
  }
  
  if (buttonPressed && buttonState) {
    // Detectar pulsación larga (> 1 segundo)
    if (!longPressDetected && (millis() - buttonPressTime > 1000)) {
      longPressDetected = true;
      // Pulsación larga: iniciar calentamiento
      playBeep();
      delay(100);
      playBeep();
      
      switch(selectedMode) {
        case MODE_TE: targetTemp = TEMP_TE; break;
        case MODE_CAFE: targetTemp = TEMP_CAFE; break;
        case MODE_MATE: targetTemp = TEMP_MATE; break;
        case MODE_HERVIR: targetTemp = TEMP_HERVIR; break;
        default: targetTemp = TEMP_HERVIR;
      }
      
      isHeating = true;
      digitalWrite(RELAY_PIN, HIGH);
      animationStep = 0;
    }
  }
  
  if (!buttonState && buttonPressed) {
    // Botón liberado
    if (!longPressDetected) {
      // Pulsación corta: cambiar modo
      playBeep();
      
      if (currentMode == MODE_CLOCK) {
        currentMode = MODE_TE;
        selectedMode = MODE_TE;
      } else {
        switch(selectedMode) {
          case MODE_TE: selectedMode = MODE_CAFE; break;
          case MODE_CAFE: selectedMode = MODE_MATE; break;
          case MODE_MATE: selectedMode = MODE_HERVIR; break;
          case MODE_HERVIR: selectedMode = MODE_TE; break;
        }
        currentMode = selectedMode;
      }
    }
    buttonPressed = false;
  }
}

void updateDisplay() {
  if (currentMode == MODE_CLOCK) {
    // Mostrar hora
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      int displayTime = timeinfo.tm_hour * 100 + timeinfo.tm_min;
      display.showNumberDecEx(displayTime, 0b01000000, true);
    }
  } else {
    // Mostrar temperatura objetivo
    int temp = 0;
    switch(currentMode) {
      case MODE_TE: temp = TEMP_TE; break;
      case MODE_CAFE: temp = TEMP_CAFE; break;
      case MODE_MATE: temp = TEMP_MATE; break;
      case MODE_HERVIR: temp = TEMP_HERVIR; break;
      default: temp = 0;
    }
    display.showNumberDec(temp, false);
  }
}

void showAnimation() {
  // Animación tipo "snake"
  uint8_t segments[] = {0x00, 0x00, 0x00, 0x00};
  
  int pos = animationStep % 6;
  int digit = pos / 2;
  bool isTop = (pos % 2) == 0;
  
  if (isTop) {
    segments[digit] = 0b00000001; // Segmento superior
  } else {
    segments[digit] = 0b00001000; // Segmento inferior
  }
  
  display.setSegments(segments);
  animationStep++;
}

void playStartupMelody() {
  tone(BUZZER_PIN, NOTE_C5, 150);
  delay(150);
  tone(BUZZER_PIN, NOTE_E5, 150);
  delay(150);
  tone(BUZZER_PIN, NOTE_G5, 150);
  delay(150);
  tone(BUZZER_PIN, NOTE_C6, 300);
  delay(300);
  noTone(BUZZER_PIN);
}

void playBeep() {
  tone(BUZZER_PIN, NOTE_A5, 100);
  delay(100);
  noTone(BUZZER_PIN);
}

void playCompleteMelody() {
  tone(BUZZER_PIN, NOTE_G5, 200);
  delay(200);
  tone(BUZZER_PIN, NOTE_C6, 200);
  delay(200);
  tone(BUZZER_PIN, NOTE_E5, 200);
  delay(200);
  tone(BUZZER_PIN, NOTE_G5, 400);
  delay(400);
  noTone(BUZZER_PIN);
}

String getModeName(TempMode mode) {
  switch(mode) {
    case MODE_TE: return "Té";
    case MODE_CAFE: return "Café";
    case MODE_MATE: return "Mate";
    case MODE_HERVIR: return "Hervir";
    default: return "Reloj";
  }
}

void setupWebServer() {
  // Página principal
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    const char* html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Pava Electrica Inteligente</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 20px;
    }
    .container {
      background: white;
      border-radius: 20px;
      padding: 40px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.3);
      max-width: 500px;
      width: 100%;
    }
    h1 {
      text-align: center;
      color: #333;
      margin-bottom: 30px;
      font-size: 28px;
    }
    .gauge-container {
      position: relative;
      width: 250px;
      height: 250px;
      margin: 30px auto;
    }
    svg { width: 100%; height: 100%; }
    .gauge-bg { fill: none; stroke: #e0e0e0; stroke-width: 20; }
    .gauge-fill { 
      fill: none; 
      stroke: #667eea; 
      stroke-width: 20; 
      stroke-linecap: round;
      transition: stroke-dasharray 0.3s ease;
    }
    .temp-display {
      position: absolute;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      text-align: center;
    }
    .temp-value {
      font-size: 48px;
      font-weight: bold;
      color: #333;
    }
    .temp-unit { font-size: 24px; color: #666; }
    .info-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 15px;
      margin-top: 30px;
    }
    .info-card {
      background: #f5f5f5;
      padding: 20px;
      border-radius: 10px;
      text-align: center;
    }
    .info-label {
      font-size: 14px;
      color: #666;
      margin-bottom: 5px;
    }
    .info-value {
      font-size: 20px;
      font-weight: bold;
      color: #333;
    }
    .status {
      margin-top: 20px;
      padding: 15px;
      border-radius: 10px;
      text-align: center;
      font-weight: bold;
      font-size: 18px;
    }
    .status.heating {
      background: #ffebee;
      color: #c62828;
    }
    .status.idle {
      background: #e8f5e9;
      color: #2e7d32;
    }
    .controls {
      margin-top: 30px;
    }
    .controls h2 {
      font-size: 18px;
      color: #333;
      margin-bottom: 15px;
      text-align: center;
    }
    .button-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
    }
    .temp-button {
      padding: 15px;
      border: 2px solid #667eea;
      border-radius: 10px;
      background: white;
      color: #667eea;
      font-weight: bold;
      font-size: 16px;
      cursor: pointer;
      transition: all 0.3s ease;
    }
    .temp-button:hover {
      background: #f0f0f0;
      transform: translateY(-2px);
    }
    .temp-button:active {
      transform: translateY(0);
    }
    .temp-button.active {
      background: #667eea;
      color: white;
    }
    .temp-button:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
    .stop-button {
      margin-top: 15px;
      width: 100%;
      padding: 15px;
      border: none;
      border-radius: 10px;
      background: #c62828;
      color: white;
      font-weight: bold;
      font-size: 16px;
      cursor: pointer;
      transition: all 0.3s ease;
    }
    .stop-button:hover {
      background: #b71c1c;
    }
    .stop-button:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
    .about {
      margin-top: 30px;
      padding: 20px;
      background: #f9f9f9;
      border-radius: 10px;
      border-left: 4px solid #667eea;
    }
    .about h3 {
      font-size: 16px;
      color: #333;
      margin-bottom: 15px;
    }
    .about ul {
      list-style: none;
      padding: 0;
    }
    .about li {
      padding: 8px 0;
      color: #666;
      font-size: 14px;
      display: flex;
      align-items: center;
    }
    .about li:before {
      content: "•";
      color: #667eea;
      font-weight: bold;
      font-size: 20px;
      margin-right: 10px;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Pava Electrica</h1>
    
    <div class="gauge-container">
      <svg viewBox="0 0 200 200">
        <circle class="gauge-bg" cx="100" cy="100" r="80"/>
        <circle id="gauge" class="gauge-fill" cx="100" cy="100" r="80"
                transform="rotate(-90 100 100)"
                stroke-dasharray="0 502"/>
      </svg>
      <div class="temp-display">
        <div class="temp-value" id="temp">--</div>
        <div class="temp-unit">&deg;C</div>
      </div>
    </div>
    
    <div class="info-grid">
      <div class="info-card">
        <div class="info-label">Modo</div>
        <div class="info-value" id="mode">--</div>
      </div>
      <div class="info-card">
        <div class="info-label">Objetivo</div>
        <div class="info-value" id="target">--</div>
      </div>
    </div>
    
    <div id="status" class="status idle">Esperando</div>

    <div class="controls">
      <h2>Control de Temperatura</h2>
      <div class="button-grid">
        <button class="temp-button" onclick="startHeating(70)" id="btn-te">Te (70&deg;C)</button>
        <button class="temp-button" onclick="startHeating(90)" id="btn-cafe">Cafe (90&deg;C)</button>
        <button class="temp-button" onclick="startHeating(80)" id="btn-mate">Mate (80&deg;C)</button>
        <button class="temp-button" onclick="startHeating(100)" id="btn-hervir">Hervir (100&deg;C)</button>
      </div>
      <button class="stop-button" onclick="stopHeating()" id="btn-stop">Detener</button>
    </div>

    <div class="about">
      <h3>Acerca de esta Pava</h3>
      <ul>
        <li>Control inteligente basado en ESP32</li>
        <li>Sensor de temperatura DS18B20 de precision</li>
        <li>Display TM1637 con hora sincronizada por NTP</li>
        <li>4 temperaturas preestablecidas optimizadas</li>
        <li>Control tactil con pulsador multifuncion</li>
        <li>Interfaz web responsive para control remoto</li>
        <li>Alertas sonoras con buzzer integrado</li>
        <li>Animacion visual durante el calentamiento</li>
      </ul>
    </div>
  </div>

  <script>
    let currentHeating = false;

    function updateData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          document.getElementById('temp').textContent = data.temp.toFixed(1);
          document.getElementById('mode').textContent = data.mode;
          document.getElementById('target').textContent = data.target + ' C';
          
          currentHeating = data.heating;
          
          const status = document.getElementById('status');
          if (data.heating) {
            status.textContent = 'Calentando...';
            status.className = 'status heating';
          } else {
            status.textContent = 'Lista';
            status.className = 'status idle';
          }
          
          const percent = (data.temp / 100) * 502;
          document.getElementById('gauge').setAttribute('stroke-dasharray', percent + ' 502');

          updateButtons(data.heating, data.target);
        });
    }

    function updateButtons(heating, target) {
      const buttons = document.querySelectorAll('.temp-button');
      buttons.forEach(btn => {
        btn.disabled = heating;
        btn.classList.remove('active');
      });

      if (heating) {
        if (target == 70) document.getElementById('btn-te').classList.add('active');
        else if (target == 90) document.getElementById('btn-cafe').classList.add('active');
        else if (target == 80) document.getElementById('btn-mate').classList.add('active');
        else if (target == 100) document.getElementById('btn-hervir').classList.add('active');
      }

      document.getElementById('btn-stop').disabled = !heating;
    }

    function startHeating(temp) {
      fetch('/start?temp=' + temp)
        .then(() => updateData())
        .catch(err => console.error('Error:', err));
    }

    function stopHeating() {
      fetch('/stop')
        .then(() => updateData())
        .catch(err => console.error('Error:', err));
    }
    
    updateData();
    setInterval(updateData, 1000);
  </script>
</body>
</html>
)rawliteral";
    request->send(200, "text/html", html);
  });
  
  // API endpoint para datos
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"temp\":" + String(currentTemp) + ",";
    json += "\"mode\":\"" + getModeName(currentMode) + "\",";
    json += "\"target\":" + String(targetTemp) + ",";
    json += "\"heating\":" + String(isHeating ? "true" : "false");
    json += "}";
    request->send(200, "application/json", json);
  });

  // Endpoint para iniciar calentamiento
  server.on("/start", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("temp")) {
      int temp = request->getParam("temp")->value().toInt();
      if (temp == 70 || temp == 80 || temp == 90 || temp == 100) {
        targetTemp = temp;
        isHeating = true;
        digitalWrite(RELAY_PIN, HIGH);
        animationStep = 0;
        playBeep();
        request->send(200, "text/plain", "OK");
      } else {
        request->send(400, "text/plain", "Invalid temperature");
      }
    } else {
      request->send(400, "text/plain", "Missing temperature parameter");
    }
  });

  // Endpoint para detener calentamiento
  server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *request){
    isHeating = false;
    digitalWrite(RELAY_PIN, LOW);
    currentMode = MODE_CLOCK;
    playBeep();
    request->send(200, "text/plain", "OK");
  });
}