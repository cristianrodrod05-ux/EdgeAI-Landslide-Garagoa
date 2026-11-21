#include <Wire.h>
#include <math.h>

// Pines exactos para ESP32-S3 DevKitC-1
const int PIN_HUMEDAD = 4;    // GPIO 4 (ADC1)
const int PIN_ALARMA  = 2;    // GPIO 2 (LED / Buzzer)
const int MPU_ADDR    = 0x68; // Dirección I2C del MPU6050

// Frecuencia de muestreo: 20 Hz = 1 lectura cada 50 ms
const unsigned long INTERVALO_MS = 50;
unsigned long tiempoAnterior = 0;

// Variables para medir latencia de ejecución (Arquitectura del Computador)
unsigned long latencia_us = 0;
int contador_ciclos = 0;

// =========================================================================
// ESTRUCTURA DE DATOS: BUFFER CIRCULAR FIFO (Complejidad O(1))
// =========================================================================
#define WINDOW_SIZE 50

struct MuestraSensor {
  float ax, ay, az;
  int humedad_adc;
};

class CircularBuffer {
  private:
    MuestraSensor buffer[WINDOW_SIZE];
    int head = 0;
    int count = 0;

  public:
    // Inserción en tiempo constante O(1) sin usar memoria dinámica
    void push(MuestraSensor dato) {
      buffer[head] = dato;
      head = (head + 1) % WINDOW_SIZE;
      if (count < WINDOW_SIZE) count++;
    }

    bool isFull() {
      return count == WINDOW_SIZE;
    }

    float promedioHumedadPct() {
      if (count == 0) return 0.0;
      long suma = 0;
      for (int i = 0; i < count; i++) {
        suma += buffer[i].humedad_adc;
      }
      return ((float)suma / count / 4095.0) * 100.0;
    }
};

CircularBuffer ventanaEdge;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_ALARMA, OUTPUT);
  digitalWrite(PIN_ALARMA, LOW);

  // Bus I2C en pines 8 (SDA) y 9 (SCL) de la ESP32-S3
  Wire.begin(8, 9);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  Serial.println("SISTEMA INICIADO - ESP32-S3 LISTA");
}

void loop() {
  unsigned long tiempoActual = millis();

  if (tiempoActual - tiempoAnterior >= INTERVALO_MS) {
    tiempoAnterior = tiempoActual;

    // Inicio de cronómetro de latencia en microsegundos
    unsigned long t_inicio = micros();

    // 1. Leer aceleración del MPU6050
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x3B);
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_ADDR, 6, true);

    int16_t raw_ax = Wire.read() << 8 | Wire.read();
    int16_t raw_ay = Wire.read() << 8 | Wire.read();
    int16_t raw_az = Wire.read() << 8 | Wire.read();

    MuestraSensor lectura;
    lectura.ax = raw_ax / 16384.0;
    lectura.ay = raw_ay / 16384.0;
    lectura.az = raw_az / 16384.0;
    lectura.humedad_adc = analogRead(PIN_HUMEDAD);

    // 2. Guardar en Buffer Circular O(1)
    ventanaEdge.push(lectura);

    // 3. Alerta local preventiva en el Edge
    if (ventanaEdge.isFull()) {
      float hum_pct = ventanaEdge.promedioHumedadPct();
      float pitch = atan2(lectura.ax, sqrt(lectura.ay * lectura.ay + lectura.az * lectura.az)) * 180.0 / PI;
      
      if (hum_pct > 80.0 && abs(pitch) > 38.0) {
        digitalWrite(PIN_ALARMA, HIGH);
      } else {
        digitalWrite(PIN_ALARMA, LOW);
      }
    }

    // Fin de cronómetro: mide cuánto tardó el ESP32 en leer, guardar y evaluar
    latencia_us = micros() - t_inicio;
    contador_ciclos++;

    // 4. Enviar trama limpia al ETL de Cristian (4 columnas exactas)
    Serial.print(lectura.ax, 4); Serial.print(",");
    Serial.print(lectura.ay, 4); Serial.print(",");
    Serial.print(lectura.az, 4); Serial.print(",");
    Serial.println(lectura.humedad_adc);

    // Cada 100 ciclos (5 segundos) reporta la latencia sin dañar el CSV de Cristian
    // (El script de Python ignora esta línea porque no tiene 4 comas)
    if (contador_ciclos >= 100) {
      Serial.print("INFO_LATENCIA_US: ");
      Serial.println(latencia_us);
      contador_ciclos = 0;
    }
  }
}