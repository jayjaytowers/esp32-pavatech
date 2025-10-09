#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TM1637Display.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Pines
#define RELE        15
#define SENSOR      13
#define DISPLAY_CLK 22
#define DISPLAY_DIO 23
#define PULSADOR    12

// Credenciales Wi-Fi Wokwi
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL 6 // Defining the WiFi channel speeds up the connection

const uint8_t SEG_HOLA [] = {
	SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,           // H
	SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,   // O
	SEG_D | SEG_E | SEG_F,                           // L
	SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G    // A
};

// Configuración del sensor DS18B20
OneWire oneWire (SENSOR);
DallasTemperature sensors (&oneWire);

// Configuración del display TM1637
TM1637Display display (DISPLAY_CLK, DISPLAY_DIO);

// Configuración NTP
WiFiUDP ntpUDP;
NTPClient timeClient (ntpUDP, "ar.pool.ntp.org", -10800, 60000); // UTC-3

// Configuración del servidor web
WebServer server (80);

// Variables para el pulsador y modos
bool displayMode = false; // false: temperatura, true: hora
int lastButtonState = HIGH; // Estado inicial del pulsador (asumiendo pull-up)

// Página HTML embebida
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Control de Pava Eléctrica</title>
  <script src="https://cdnjs.cloudflare.com/ajax/libs/raphael/2.3.0/raphael.min.js"></script>
  <script src="https://cdnjs.cloudflare.com/ajax/libs/justgage/1.4.0/justgage.min.js"></script>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; padding: 50px; }
    button { padding: 15px 30px; margin: 10px; font-size: 18px; cursor: pointer; }
    #gauge { width: 300px; height: 240px; margin: 20px auto; }
  </style>
</head>
<body>
  <h1>Control de Pava Eléctrica</h1>
  <button id="controlButton" onclick="toggleCommand()">Cargando...</button>
  <div id="gauge" data-value="0"></div>

  <script>
    let gauge = new JustGage({
      id: 'gauge',
      value: 0,
      min: 0,
      max: 100,
      title: 'Temperatura (°C)',
      label: '°C',
      gaugeWidthScale: 0.6,
      counter: true,
      decimals: 2,
      gaugeColor: '#00ff00' // Verde por defecto (apagada)
    });

    // Actualizar estado inicial y texto del botón
    function updateStatus() {
      fetch('/status')
        .then(response => response.text())
        .then(data => {
          const button = document.getElementById('controlButton');
          button.innerText = (data === 'ENCENDIDA') ? 'Apagar' : 'Encender';
          gauge.refresh(gauge._getValue(), null, null, {
            gaugeColor: data === 'ENCENDIDA' ? '#ff0000' : '#00ff00' // Rojo si encendida, verde si apagada
          });
        });
    }
    updateStatus();

    // Actualizar temperatura y estado cada 5 segundos
    setInterval(() => {
      fetch('/temperature')
        .then(response => response.text())
        .then(data => {
          gauge.refresh(parseFloat(data));
        });
      updateStatus(); // Actualizar estado y color del gauge
    }, 5000);

    function toggleCommand() {
      const button = document.getElementById('controlButton');
      const command = button.innerText === 'Encender' ? 'ENCENDER' : 'APAGAR';
      fetch('/control?command=' + command)
        .then(response => response.text())
        .then(data => {
          button.innerText = (data === 'ENCENDIDA') ? 'Apagar' : 'Encender';
          gauge.refresh(gauge._getValue(), null, null, {
            gaugeColor: data === 'ENCENDIDA' ? '#ff0000' : '#00ff00'
          });
        });
    }
  </script>
</body>
</html>
)rawliteral";

// Función para formatear y mostrar la temperatura
void mostrarTemperatura (float temp) {
  // Convertir la temperatura a entero (redondeo)
  int tempInt = round(temp);
  
  // Asegurarse de que la temperatura esté en el rango de 0 a 99
  if (tempInt < 0) tempInt = 0;
  if (tempInt > 99) tempInt = 99;

  // Crear el array para el display (4 posiciones)
  uint8_t data[] = {0, 0, 0, 0};

  // Primeros dos dígitos: temperatura
  data[0] = display.encodeDigit(tempInt / 10); // Decenas
  data[1] = display.encodeDigit(tempInt % 10); // Unidades
  
  // Tercer dígito: símbolo °
  data[2] = SEG_A | SEG_B | SEG_F | SEG_G; // °
  
  // Cuarto dígito: letra C
  data[3] = SEG_A | SEG_D | SEG_E | SEG_F; // C

  // Mostrar en el display
  display.setSegments(data);
}

void setup() {
  Serial.begin (115200);
  pinMode (RELE, OUTPUT);
  digitalWrite (RELE, LOW); // Pava apagada inicialmente
  pinMode (PULSADOR, INPUT_PULLUP);
  sensors.begin(); // Iniciar el sensor DS18B20
  display.setBrightness (0x0f); // Configurar brillo máximo del TM1637
  display.setSegments (SEG_HOLA);

  // Conectar a WiFi
  WiFi.begin (WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  while (WiFi.status() != WL_CONNECTED) {
    delay (1000);
    Serial.println ("Conectando a WiFi...");
  }
  Serial.println ("Conectado a WiFi: " + WiFi.localIP().toString());

  // Iniciar NTP
  timeClient.begin ();
  timeClient.update ();

  // Configurar rutas del servidor web
  server.on ("/", []() {
    server.sendHeader("Content-Type", "text/html; charset=UTF-8");
    server.send(200, "text/html", htmlPage);
  });
  server.on ("/temperature", []() {
    sensors.requestTemperatures();
    float temperature = sensors.getTempCByIndex(0);
    if (temperature != DEVICE_DISCONNECTED_C) {
      char tempStr[8];
      dtostrf(temperature, 6, 2, tempStr);
      server.send(200, "text/plain", tempStr);
    } else {
      server.send(500, "text/plain", "Error sensor");
    }
  });
  server.on ("/control", []() {
    String command = server.arg("command");
    if (command == "ENCENDER") {
      digitalWrite (RELE, HIGH);
      server.send (200, "text/plain", "ENCENDIDA");
    } else if (command == "APAGAR") {
      digitalWrite(RELE, LOW);
      server.send(200, "text/plain", "APAGADA");
    } else {
      server.send(400, "text/plain", "Comando inválido");
    }
  });
  server.on ("/status", []() {
    server.send(200, "text/plain", digitalRead(RELE) ? "ENCENDIDA" : "APAGADA");
  });
  server.begin ();
  Serial.println ("Servidor web iniciado.");
}

void loop() {
  server.handleClient();

  // Leer el estado del pulsador
  int buttonState = digitalRead (PULSADOR);
  if (buttonState == LOW && lastButtonState == HIGH) {
    displayMode = !displayMode; // Alternar modo
    delay (50); // Debounce simple
  }
  lastButtonState = buttonState;

  // Actualizar display cada segundo (para hora precisa)
  static unsigned long lastUpdate = 0;
  if (millis () - lastUpdate >= 1000) {
    timeClient.update (); // Actualizar hora NTP

    if (displayMode) {
      // Mostrar hora (HH:MM)
      int hour = timeClient.getHours();
      int minute = timeClient.getMinutes();
      display.showNumberDecEx(hour * 100 + minute, 0x40, true); // Mostrar con dos puntos
      Serial.println("Hora: " + String(hour) + ":" + String(minute));
    } else {
      // Mostrar temperatura (parte entera + °C)
      sensors.requestTemperatures();
      float temperature = sensors.getTempCByIndex(0);
      if (temperature != DEVICE_DISCONNECTED_C) {
        int tempInt = (int)temperature; // Tomar solo la parte entera
        uint8_t data[] = {
          display.encodeDigit(tempInt / 10), // Decenas
          display.encodeDigit(tempInt % 10), // Unidades
          SEG_A | SEG_B | SEG_F | SEG_G,     // Símbolo de grados
          SEG_A | SEG_D | SEG_E | SEG_F      // Letra "C"
        };
        display.setSegments(data); // Mostrar "XX°C"
        Serial.println("Temperatura: " + String(temperature) + " °C");
      } else {
        display.showNumberDec(0); // Mostrar 0 si hay error
        Serial.println("Error al leer el sensor DS18B20");
      }
    }
    lastUpdate = millis();
  }
}