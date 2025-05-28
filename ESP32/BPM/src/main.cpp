#include "FS.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#include "MAX30105.h"
#include "heartRate.h"

MAX30105 particleSensor;

const byte RATE_SIZE = 12;  // Tamaño del buffer para mayor precisión
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;

float beatsPerMinute;
int beatAvg;
int stableBPM = 0;  // Valor de BPM estabilizado
int previousBPM = 0; // Almacena el BPM anterior
unsigned long lastUpdateTime = 0;
const unsigned long stabilizationInterval = 5000;  // Estabilización cada 5 segundos

// Variables para manejar el número de mediciones iniciales
const int minValidMeasurements = 5;  // Número de mediciones válidas necesarias
int validMeasurements = 0;

bool dedoPresente = false;
const int umbralIR = 60000;  // Ajustar este valor según la sensibilidad deseada
const int tiempoConfirmacionDedo = 2000;  // 2 segundos para confirmar presencia de dedo
unsigned long tiempoUltimaConfirmacion = 0;

void mostrarMensaje(String mensaje, int textSize, int yPos);
int calcularPromedioBPM();
void mostrarBPM(int bpm);

void setup() {
  Serial.begin(115200);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (1);
  }
  display.clearDisplay();
  display.display();

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30105 was not found. Please check wiring/power.");
    while (1);
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(80);  // Configurar para detectar pulso
  particleSensor.setPulseAmplitudeGreen(0); // Desactivar LED verde
}

void loop() {
  long irValue = particleSensor.getIR();

  // Confirmar presencia de dedo durante tiempoConfirmacionDedo antes de comenzar a calcular BPM
  if (irValue >= umbralIR) {
    if (!dedoPresente && (millis() - tiempoUltimaConfirmacion > tiempoConfirmacionDedo)) {
      dedoPresente = true;  // Confirmar que el dedo está colocado
      Serial.println("Dedo detectado");
      display.clearDisplay();
      display.display();
    }
  } else {
    dedoPresente = false;
    tiempoUltimaConfirmacion = millis();
    mostrarMensaje("Ubique su dedo", 2, 10);
    stableBPM = 0;
    validMeasurements = 0;  // Reiniciar las mediciones válidas
    return;
  }

  // Si hay latido, calcular BPM
  if (dedoPresente && checkForBeat(irValue)) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    beatsPerMinute = 60 / (delta / 1000.0);

    // Solo registrar BPM si está dentro del rango válido
    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;

      // Filtro para excluir valores atípicos en las primeras mediciones
      if (validMeasurements < minValidMeasurements) {
        if (beatsPerMinute >= 50 && beatsPerMinute <= 180) {  // Solo aceptar BPM razonables
          validMeasurements++;  // Contar como medición válida
        }
      }

      // Solo promediar después de tener suficientes mediciones válidas
      if (validMeasurements >= minValidMeasurements) {
        beatAvg = calcularPromedioBPM();
      }
    }
  }

  // Solo actualizar BPM estabilizado después de tener un valor válido
  if (validMeasurements >= minValidMeasurements) {
    // Actualizar BPM estabilizado cada "stabilizationInterval"
    unsigned long currentTime = millis();
    if (currentTime - lastUpdateTime >= stabilizationInterval && beatAvg != 0) {
      stableBPM = beatAvg;

      // Filtrar cambios pequeños antes de actualizar
      if (abs(stableBPM - previousBPM) >= 1) {  // Cambio mínimo para actualizar
        previousBPM = stableBPM;
        lastUpdateTime = currentTime;

        mostrarBPM(stableBPM);  // Mostrar BPM estabilizado
      }
    }
  }
}

// Función para mostrar mensajes en la pantalla
void mostrarMensaje(String mensaje, int textSize, int yPos) {
  display.clearDisplay();
  display.setTextSize(textSize);  // Usar el parámetro textSize
  display.setCursor(10, yPos);
  display.print(mensaje);
  display.display();
  Serial.println(mensaje);
}

// Función para calcular el promedio de los BPM
int calcularPromedioBPM() {
  int sumaBPM = 0;
  for (byte i = 0; i < RATE_SIZE; i++) {
    sumaBPM += rates[i];
  }
  return sumaBPM / RATE_SIZE;
}

// Función para mostrar el BPM estabilizado en pantalla
void mostrarBPM(int bpm) {
  display.clearDisplay();
  display.setTextSize(4);
  display.setCursor(35, 10);
  display.println(bpm);
  display.display();
  Serial.print("BPM estabilizado: ");
  Serial.println(bpm);
}