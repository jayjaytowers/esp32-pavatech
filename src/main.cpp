#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TM1637Display.h>

// Pines
#define RELE        15
#define SENSOR      32
#define DISPLAY_CLK 22
#define DISPLAY_DIO 23

const uint8_t SEG_HOLA [] = {
	SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,           // H
	SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,   // O
	SEG_D | SEG_E | SEG_F,                           // L
	SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G    // A
};

// Credenciales Wi-Fi Wokwi
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// Configuración del sensor DS18B20
OneWire oneWire(SENSOR);
DallasTemperature sensors(&oneWire);

// Configuración del display TM1637
TM1637Display display(DISPLAY_CLK, DISPLAY_DIO);

// Configuración del servidor web
WebServer server(80);

// Página HTML embebida
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Interfaz de Pavatech</title>
  <script src="https://cdnjs.cloudflare.com/ajax/libs/raphael/2.3.0/raphael.min.js"></script>
  <script src="https://cdnjs.cloudflare.com/ajax/libs/justgage/1.4.0/justgage.min.js"></script>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; padding: 50px; }
    button { padding: 15px 30px; margin: 10px; font-size: 18px; cursor: pointer; }
    #gauge { width: 300px; height: 240px; margin: 20px auto; }
  </style>
</head>
<body>
  <h1>Pavatech</h1>
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
  Serial.begin(115200);
  pinMode(RELE, OUTPUT);
  digitalWrite(RELE, LOW); // Pava apagada inicialmente
  sensors.begin(); // Iniciar el sensor DS18B20
  display.setBrightness(0x0f); // Configurar brillo máximo del TM1637
  display.setSegments (SEG_HOLA);

  // Conectar a WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando a WiFi...");
  }
  Serial.println("Conectado a WiFi: " + WiFi.localIP().toString());

  // Configurar rutas del servidor web
  server.on("/", []() {
    server.send(200, "text/html", htmlPage);
  });
  server.on("/temperature", []() {
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
  server.on("/control", []() {
    String command = server.arg("command");
    if (command == "ENCENDER") {
      digitalWrite(RELE, HIGH);
      server.send(200, "text/plain", "ENCENDIDA");
    } else if (command == "APAGAR") {
      digitalWrite(RELE, LOW);
      server.send(200, "text/plain", "APAGADA");
    } else {
      server.send(400, "text/plain", "Comando inválido");
    }
  });
  server.on("/status", []() {
    server.send(200, "text/plain", digitalRead(RELE) ? "ENCENDIDA" : "APAGADA");
  });
  server.begin();
  Serial.println("Servidor web iniciado");
}

void loop() {
  server.handleClient();

  // Actualizar temperatura cada 5 segundos
  static unsigned long lastTempRead = 0;
  if (millis() - lastTempRead >= 5000) {
    sensors.requestTemperatures();
    float temperatura = sensors.getTempCByIndex(0);
    if (temperatura != DEVICE_DISCONNECTED_C) {
      // Mostrar temperatura en el display TM1637
      mostrarTemperatura (temperatura);
      Serial.println("Temperatura: " + String(temperatura) + " °C");
    } else {
      display.showNumberDec(0); // Mostrar 0 si hay error
      Serial.println("Error al leer el sensor DS18B20");
    }
    lastTempRead = millis();
  }
}