// Habitación que regula la "tecnadicción": modo día / modo noche
// Librerías: PubSubClient (Nick O'Leary) y LiquidCrystal_I2C (Frank de Brabander)
//
// CONEXIONES
//  LDR:  3.3V -> LDR -> GPIO34 -> resistencia 10k -> GND
//  LCD I2C 16x2: VCC -> 5V (VIN), GND -> GND, SDA -> GPIO21, SCL -> GPIO22
//  LEDs: cada uno con resistencia de 220 ohm a GND, ánodos en los pines de LED_PINS

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const char* WIFI_SSID = "TU_WIFI";
const char* WIFI_PASS = "TU_CLAVE";

const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
const char* BASE = "mi-esp32-ab12cd34";   // IDÉNTICO al de index.html

// --- Hardware ---
const int LDR_PIN = 34;
const int LED_PINS[8] = {13, 12, 14, 27, 26, 25, 33, 32};
LiquidCrystal_I2C lcd(0x27, 16, 2);       // si no se ve nada, probá con 0x3F

// --- Ajustes ---
// Con el divisor de arriba: más luz = valor más alto (0 a 4095).
// Ajustá estos dos números mirando el valor "ldr" que muestra la página.
const int UMBRAL_OSCURO = 1200;           // por debajo -> noche
const int UMBRAL_CLARO  = 1800;           // por encima -> día (histéresis, evita parpadeos)
const unsigned long DURACION_LEDS_MS = 2UL * 60UL * 1000UL;   // 2 minutos

// --- Estado ---
enum Modo { DIA, NOCHE };
Modo modo = DIA;
bool ledsEncendidos = false;
unsigned long inicioNoche = 0;
unsigned long ultimoSegundo = 0;

String T_STATUS, T_ESTADO;
WiFiClient wifi;
PubSubClient mqtt(wifi);

void ponerLeds(bool on) {
  ledsEncendidos = on;
  for (int i = 0; i < 8; i++) digitalWrite(LED_PINS[i], on ? HIGH : LOW);
}

void mostrar(const char* l1, const char* l2) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(l1);
  lcd.setCursor(0, 1); lcd.print(l2);
}

void entrarModoDia() {
  modo = DIA;
  ponerLeds(false);
  mostrar("Compu encendida", "Modo dia");
}

void entrarModoNoche() {
  modo = NOCHE;
  inicioNoche = millis();
  ponerLeds(true);
  mostrar("Hora de dormir", "");
}

int segundosRestantes() {
  if (modo != NOCHE || !ledsEncendidos) return 0;
  unsigned long t = millis() - inicioNoche;
  if (t >= DURACION_LEDS_MS) return 0;
  return (DURACION_LEDS_MS - t + 999) / 1000;
}

void actualizarLinea2() {
  if (modo != NOCHE) return;
  char buf[17];
  if (ledsEncendidos) {
    int s = segundosRestantes();
    snprintf(buf, sizeof(buf), "LEDs off: %d:%02d ", s / 60, s % 60);
  } else {
    snprintf(buf, sizeof(buf), "LEDs apagados   ");
  }
  lcd.setCursor(0, 1);
  lcd.print(buf);
}

void publicarEstado(int ldr) {
  char json[120];
  snprintf(json, sizeof(json),
           "{\"modo\":\"%s\",\"ldr\":%d,\"leds\":%s,\"restante\":%d}",
           modo == DIA ? "dia" : "noche", ldr,
           ledsEncendidos ? "true" : "false", segundosRestantes());
  mqtt.publish(T_ESTADO.c_str(), json);
}

void conectarMQTT() {
  while (!mqtt.connected()) {
    String id = "esp32-hab-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    if (mqtt.connect(id.c_str(), NULL, NULL, T_STATUS.c_str(), 1, true, "offline")) {
      mqtt.publish(T_STATUS.c_str(), "online", true);
    } else {
      delay(3000);
    }
  }
}

void setup() {
  for (int i = 0; i < 8; i++) pinMode(LED_PINS[i], OUTPUT);
  analogReadResolution(12);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  mostrar("Conectando WiFi", "");

  T_STATUS = String(BASE) + "/status";
  T_ESTADO = String(BASE) + "/estado";

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);

  // Modo inicial según la luz actual
  int ldr = analogRead(LDR_PIN);
  if (ldr < UMBRAL_OSCURO) entrarModoNoche(); else entrarModoDia();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); delay(2000); }
  else {
    if (!mqtt.connected()) conectarMQTT();
    mqtt.loop();
  }

  int ldr = analogRead(LDR_PIN);

  // Cambio de modo con histéresis
  if (modo == DIA && ldr < UMBRAL_OSCURO) entrarModoNoche();
  else if (modo == NOCHE && ldr > UMBRAL_CLARO) entrarModoDia();

  // En modo noche, apagar los LEDs al cumplirse los 2 minutos
  if (modo == NOCHE && ledsEncendidos && millis() - inicioNoche >= DURACION_LEDS_MS) {
    ponerLeds(false);
  }

  // Una vez por segundo: refrescar LCD y publicar estado
  if (millis() - ultimoSegundo >= 1000) {
    ultimoSegundo = millis();
    actualizarLinea2();
    if (mqtt.connected()) publicarEstado(ldr);
  }
}
