#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <WiFi.h>
#include <algorithm> // Incluye esta librería para std::sort #include "MAX30105.h"
#include "heartRate.h"
#include <Adafruit_GFX.h>
MAX30105 particleSensor;

const byte RATE_SIZE = 2; // Tamaño del buffer para mayor precisión byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute;
int beatAvg;
int stableBPM = 0;   // Valor de BPM estabilizado
int previousBPM = 0; // Almacena el BPM anterior
unsigned long lastUpdateTime = 0;
const unsigned long stabilizationInterval = 5000; // Estabilización cada 5 segundos

// Variables para manejar el número de mediciones iniciales
const int minValidMeasurements = 5; // Número de mediciones válidas necesarias
int validMeasurements = 0;
bool dedoPresente = false;
const int umbralIR = 60000;              // Ajustar este valor según la sensibilidad deseada
const int tiempoConfirmacionDedo = 2000; // 2 segundos para confirmar presencia de dedo
unsigned long tiempoUltimaConfirmacion = 0;

// WiFi config
const char *WiFi_SSID = "...";
const char *PASSWORD = "...";

void mostrarMensaje(String mensaje);
int calcularPromedioBPM();
void mostrarBPM(int bpm);
void detectarDispositivosI2C();
void resetI2CPins();

////////////////////////FIREBASEEEEEEEEEE/////////////////////////////

const char *DB_URL = "https://pulsemonitor-ebc21-default-rtdb.firebaseio.com/";
const char *API_WEB_KEY = "AIzaSyDZXpmTZj7mjebvJOKndqCtt9dFj0wOVs4";
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
unsigned long sendDataPrevMillis = 0;
bool signupOk = false;

/////////////////////////////////////////////////////////////////////

void setup()
{

  Serial.begin(115200);
  tft.initR(INITR_BLACKTAB);      // Inicializa la pantalla con configuración básica
  tft.setRotation(1);             // Ajusta la orientación según sea necesario
  tft.fillScreen(ST77XX_BLACK);   // Limpia la pantalla con color negro
  tft.setTextColor(ST77XX_WHITE); // Texto blanco
  WiFi.begin(WiFi_SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Conectando a WiFi...");
  }
  Serial.println("\nConexión establecida");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());

  // CONFIG AUTH Y HOST DE FB
  config.api_key = API_WEB_KEY;

  config.database_url = DB_URL;
  auth.user.email = "";
  auth.user.password = "";

  if (Firebase.signUp(&config, &auth, "", ""))
  {
    Serial.println("Firebase SignUp OK");
    signupOk = true;
  }
  else
  {
    Serial.println(config.signer.signupError.message.c_str());
  }
  // INICIO DE FB
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Firebase.RTDB.setBool(&fbdo, "Enlinea", true);
  Serial.println('Sistema Conectado');
  Wire.begin(21, 22); // Reemplaza los números según los pines reales si son diferentes
  detectarDispositivosI2C();
  if (!particleSensor.begin(Wire, 100000))
  {
    Serial.println("MAX30105 was not found. Please check wiring/power.");
    while (1)
      ;
  }
  else
  {
    Serial.println("MAX30105 detectado correctamente.");
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(80);  // Configurar para detectar pulso
  particleSensor.setPulseAmplitudeGreen(0); // Desactivar LED verde
}

void loop()
{
  long irValue = particleSensor.getIR();
  delay(10); // Evitar saturación del bus
  // Confirmar presencia de dedo durante tiempoConfirmacionDedo antes de comenzar a calcular BPM
  if (irValue >= umbralIR)
  {
    if (!dedoPresente && (millis() - tiempoUltimaConfirmacion > tiempoConfirmacionDedo))
    {
      dedoPresente = true; // Confirmar que el dedo está colocado Serial.println("Dedo detectado");
    }
  }
  else
  {
    if (dedoPresente)
    {
      Serial.println("Dedo removido");
    }
    dedoPresente = false;
    tiempoUltimaConfirmacion = millis();
    mostrarMensaje("Ubique su dedo");
    stableBPM = 0;
    validMeasurements = 0; // Reiniciar las mediciones válidas return;
  }
  // Si hay latido, calcular BPM
  if (dedoPresente && checkForBeat(irValue))
  {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    beatsPerMinute = 60 / (delta / 1000.0);

    // Solo registrar BPM si está dentro del rango válido
    if (beatsPerMinute < 255 && beatsPerMinute > 20)
    {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;
      // Filtro para excluir valores atípicos en las primeras mediciones
      if (validMeasurements < minValidMeasurements)
      {
        if (beatsPerMinute >= 50 && beatsPerMinute <= 180)
        {                      // Solo aceptar BPM razonables
          validMeasurements++; // Contar como medición válida
        }
      }

      // Solo promediar después de tener suficientes mediciones válidas
      if (validMeasurements >= minValidMeasurements)
      {
        beatAvg = calcularPromedioBPM();
      }
    }
  }

  // Solo actualizar BPM estabilizado después de tener un valor válido
  if (validMeasurements >= minValidMeasurements)
  {
    unsigned long currentTime = millis();
    if (currentTime - lastUpdateTime >= stabilizationInterval && beatAvg != 0)
    {
      stableBPM = beatAvg;
      if (abs(stableBPM - previousBPM) >= 1)
      {
        previousBPM = stableBPM;
        lastUpdateTime = currentTime;
        mostrarBPM(stableBPM); // Mostrar BPM estabilizado
      }
    }
  }

  if (Firebase.ready() && signupOk && (millis() - sendDataPrevMillis > 5000 || sendDataPrevMillis == 0))
  {
    sendDataPrevMillis = millis();
    if (Firebase.RTDB.setInt(&fbdo, "monitor/bpm", stableBPM))
    {
      Serial.println("Data sent successfully to Firebase.");
      mostrarBPM(stableBPM); // Mostrar BPM estabilizado
    }
    else
    {
      Serial.println(fbdo.errorReason());
    }
  }
}

// Función para mostrar mensajes en la pantalla
void mostrarMensaje(String mensaje)
{

  Serial.println(mensaje);
}

// Función para calcular el promedio de los BPM
int calcularPromedioBPM()
{
  int sumaBPM = 0;
  for (byte i = 0; i < RATE_SIZE; i++)
  {
    sumaBPM += rates[i];
  }
  return sumaBPM / RATE_SIZE;
}

// Función para mostrar el BPM estabilizado en pantalla
// Función para mostrar el BPM estabilizado en pantalla y en Serial
void mostrarBPM(int bpm)
{
  Serial.print("BPM estabilizado: ");
  Serial.println(bpm);
  // Limpiar el área de BPM para evitar texto superpuesto
  tft.fillRect(10, 100, 128, 20, ST77XX_BLACK);

  // Mostrar el BPM en la pantalla
  tft.setCursor(10, 100);
  tft.setTextSize(2); // Tamaño del texto
  tft.print("BPM: ");
  tft.print(bpm);
}

void detectarDispositivosI2C()
{
  byte error, direccion;
  int dispositivos = 0;
  Serial.println("Buscando dispositivos I2C...");
  for (direccion = 1; direccion < 127; direccion++)
  {
    Wire.beginTransmission(direccion);
    error = Wire.endTransmission();
    if (error == 0)
    {
      Serial.print("Dispositivo encontrado en 0x");
      Serial.println(direccion, HEX);
      dispositivos++;
    }
  }
  if (dispositivos == 0)
  {
    Serial.println("No se encontraron dispositivos I2C.");
  }
  else
  {
    Serial.println("Escaneo I2C completado.");
  }
}
void resetI2CPins()
{
  pinMode(21, OUTPUT);    // Cambia por los pines de tu I2C (SDA)
  pinMode(22, OUTPUT);    // Cambia por los pines de tu I2C (SCL)
  digitalWrite(21, HIGH); // Libera SDA
  digitalWrite(22, HIGH); // Libera SCL
  delay(10);

  for (int i = 0; i < 9; i++)
  { // Genera 9 pulsos en SCL
    digitalWrite(22, LOW);
    delay(10);
    digitalWrite(22, HIGH);
    delay(10);
  }
  Wire.begin(); // Reinicia el bus I2C
  Serial.println("Bus I2C reiniciado manualmente");
}
