#include <Wire.h>
#include <math.h>
#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model.h"

// =========================================================================
// 0. PINES Y CONFIGURACIÓN DE HARDWARE (ESP32-S3)
// =========================================================================
const int PIN_HUMEDAD = 4;    // GPIO 4 (ADC) - Sensor de humedad
const int PIN_ALARMA  = 2;    // GPIO 2 - Pin (+) del buzzer activo; Pin (-) a GND
const int MPU_ADDR    = 0x69; // Dirección I2C MPU6050 confirmada (SDA = 8, SCL = 9)

// Frecuencia de muestreo del sistema: 20 Hz (cada 50 ms)
const unsigned long INTERVALO_MS = 50;
unsigned long tiempoAnterior = 0;

// Intervalo de impresión en Monitor Serial (1200 ms)
const unsigned long INTERVALO_PRINT_MS = 1200;
unsigned long ultimoPrintMs = 0;
unsigned long contadorTarjetas = 0;

// Umbrales físicos de respaldo: humedad (> 60%) + vibración fuerte (>= 8.0 deg/50ms)
const float UMBRAL_HUMEDAD_MIN_PCT = 60.0f;
const float UMBRAL_VIBRACION_DEG   = 8.0f;

// =========================================================================
// 1. ESTRUCTURA DE DATOS EN EL EDGE: BUFFER CIRCULAR FIFO O(1)
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
    void push(MuestraSensor dato) {
      buffer[head] = dato;
      head = (head + 1) % WINDOW_SIZE;
      if (count < WINDOW_SIZE) count++;
    }

    bool isFull() const { return count == WINDOW_SIZE; }
    int size() const { return count; }

    float promedioHumedadPct() const {
      if (count == 0) return 0.0f;
      long suma = 0;
      for (int i = 0; i < count; i++) suma += buffer[i].humedad_adc;
      float promPctDirecto = ((float)suma / count / 4095.0f) * 100.0f;
      return constrain(100.0f - promPctDirecto, 0.0f, 100.0f);
    }

    // Filtro de media móvil (ventana W = 5) en tiempo constante O(1)
    void promedioAceleracionW(int W, float &ax_f, float &ay_f, float &az_f) const {
      if (count == 0) { ax_f = ay_f = az_f = 0.0f; return; }
      int n = (count < W) ? count : W;
      float sx = 0.0f, sy = 0.0f, sz = 0.0f;
      for (int i = 0; i < n; i++) {
        int idx = (head - 1 - i + WINDOW_SIZE) % WINDOW_SIZE;
        sx += buffer[idx].ax;
        sy += buffer[idx].ay;
        sz += buffer[idx].az;
      }
      ax_f = sx / n;
      ay_f = sy / n;
      az_f = sz / n;
    }
};

CircularBuffer ventanaEdge;

// =========================================================================
// 2. CONFIGURACIÓN TINYML (TENSORFLOW LITE MICRO INT8)
// =========================================================================
tflite::MicroErrorReporter micro_error_reporter;
const tflite::Model* tfl_model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
TfLiteTensor* output = nullptr;

constexpr int kTensorArenaSize = 4 * 1024; // 4 KB reservados en SRAM
uint8_t tensor_arena[kTensorArenaSize];

// Variables para perfilado de latencia en N = 100 inferencias
const int N_PRUEBAS = 100;
float tiempos_ms[N_PRUEBAS];
int indice_prueba = 0;

void imprimirBarra(float porcentaje, int ancho = 16) {
  float val = constrain(porcentaje, 0.0f, 100.0f);
  int llenos = (int)round((val / 100.0f) * ancho);
  Serial.print("[");
  for (int i = 0; i < ancho; i++) {
    Serial.print(i < llenos ? "█" : "░");
  }
  Serial.print("]");
}

void activarBuzzer(bool encendido) {
  digitalWrite(PIN_ALARMA, encendido ? HIGH : LOW);
}

void ejecutarEscanerI2C() {
  Serial.println("┌────────────────────────────────────────────────────────────────┐");
  Serial.println("│ 🔍 ESCÁNER DE BUS I2C (SDA = GPIO 8 | SCL = GPIO 9)            │");
  Serial.println("├────────────────────────────────────────────────────────────────┤");
  int encontrados = 0;
  bool mpuDetectado = false;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      encontrados++;
      Serial.print("│  ✔ Dispositivo detectado en dirección: 0x");
      if (addr < 16) Serial.print("0");
      Serial.print(addr, HEX);
      if (addr == MPU_ADDR) {
        Serial.println("  --> [MPU6050 OK]   │");
        mpuDetectado = true;
      } else {
        Serial.println("                   │");
      }
    }
  }

  if (encontrados == 0) {
    Serial.println("│  ✖ [!] No se detectó ningún dispositivo I2C.                   │");
    Serial.println("│      Revisa VCC, GND, SDA (GPIO 8) y SCL (GPIO 9).             │");
  } else if (!mpuDetectado) {
    Serial.print("│  ⚠ [!] Dispositivo hallado, pero NO en 0x");
    if (MPU_ADDR < 16) Serial.print("0");
    Serial.print(MPU_ADDR, HEX);
    Serial.println(" (revisa pin AD0).   │");
  }
  Serial.println("└────────────────────────────────────────────────────────────────┘");
}

void imprimirTarjetaMonitor(
  unsigned long numTarjeta,
  unsigned long tiempoMs,
  const MuestraSensor& lectura,
  float hum_pct,
  float hum_fifo_pct,
  float pitch,
  float vel_inclinacion,
  bool hayVibracion,
  bool alertaHumedad,
  bool alertaIA,
  int clase_predicha,
  bool alertaDeslizamiento,
  int8_t ml_raw,
  float ml_prob,
  float ultima_latencia_ms
) {
  float mag_g = sqrt(
    lectura.ax * lectura.ax +
    lectura.ay * lectura.ay +
    lectura.az * lectura.az
  );

  Serial.println();
  Serial.println("╔════════════════════════════════════════════════════════════════╗");
  Serial.print("║ 🏔️  NODO EDGE GARAGOA  |  Lectura #");
  Serial.print(numTarjeta);
  Serial.print("  |  ⏱️ ");
  Serial.print(tiempoMs / 1000.0f, 1);
  Serial.println(" s");
  Serial.print("║ 📦 Buffer FIFO O(1)   : ");
  Serial.print(ventanaEdge.size());
  Serial.print("/");
  Serial.print(WINDOW_SIZE);
  Serial.println(ventanaEdge.isFull() ? " muestras [✅ VENTANA LLENA]" : " muestras [⏳ LLENANDO...]");
  Serial.println("╠════════════════════════════════════════════════════════════════╣");

  // 1. Sección Humedad
  Serial.print("║ 💧 HUMEDAD SUELO      : ");
  if (hum_pct < 10.0f) Serial.print(" ");
  Serial.print(hum_pct, 1);
  Serial.print("%  ");
  imprimirBarra(hum_pct, 16);
  Serial.print(" (Umbral > ");
  Serial.print(UMBRAL_HUMEDAD_MIN_PCT, 0);
  Serial.println("%)");

  Serial.print("║    └─ Promedio FIFO   : ");
  Serial.print(hum_fifo_pct, 1);
  Serial.print("%   |   ADC Crudo: ");
  Serial.print(lectura.humedad_adc);
  Serial.println(" / 4095");
  Serial.println("╟────────────────────────────────────────────────────────────────╢");

  // 2. Sección Acelerómetro e Inclinación
  Serial.print("║ 📐 INCLINACIÓN (Pitch): ");
  if (pitch >= 0.0f) Serial.print("+");
  Serial.print(pitch, 1);
  Serial.print("°   |   ΔPitch: ");
  if (vel_inclinacion >= 0.0f) Serial.print("+");
  Serial.print(vel_inclinacion, 2);
  Serial.print("°   |   ");
  Serial.println(hayVibracion ? "📳 SACUDIDA FUERTE" : "🟢 ESTABLE");

  Serial.print("║    └─ Aceleración (g) : X=");
  if (lectura.ax >= 0.0f) Serial.print("+");
  Serial.print(lectura.ax, 2);
  Serial.print("g  Y=");
  if (lectura.ay >= 0.0f) Serial.print("+");
  Serial.print(lectura.ay, 2);
  Serial.print("g  Z=");
  if (lectura.az >= 0.0f) Serial.print("+");
  Serial.print(lectura.az, 2);
  Serial.print("g  (|a|=");
  Serial.print(mag_g, 2);
  Serial.println("g)");
  Serial.println("╟────────────────────────────────────────────────────────────────╢");

  // 3. Sección TinyML
  float scorePct = ml_prob * 100.0f;
  Serial.print("║ 🧠 TINYML (INT8)      : Riesgo C2 ");
  if (scorePct < 10.0f) Serial.print(" ");
  Serial.print(scorePct, 1);
  Serial.print("% ");
  imprimirBarra(scorePct, 10);
  Serial.print(" | Clase=");
  Serial.print(clase_predicha);
  Serial.print(" (raw=");
  Serial.print(ml_raw);
  Serial.println(")");

  Serial.print("║    └─ Inferencia Edge : ⚡ ");
  Serial.print(ultima_latencia_ms, 3);
  Serial.println(" ms  (Ejecución local ESP32-S3)");
  Serial.println("╠════════════════════════════════════════════════════════════════╣");

  // 4. Estado General y Actuador
  Serial.print("║ 🛡️  DIAGNÓSTICO       : ");
  if (alertaIA) {
    Serial.println("🚨 PELIGRO CRÍTICO (TinyML Clase 2: Deslizamiento)");
  } else if (alertaHumedad && hayVibracion) {
    Serial.println("🚨 PELIGRO CRÍTICO (Humedad > 60% + Sacudida)");
  } else if (clase_predicha == 1 || alertaHumedad) {
    Serial.println("⚠️  ALERTA PREVENTIVA (Saturación hídrica detectada)");
  } else if (hayVibracion) {
    Serial.println("⚠️  ALERTA SÍSMICA/MOV (Vibración >= 8.0°)");
  } else {
    Serial.println("✅ TERRENO ESTABLE (Parámetros normales)");
  }

  Serial.print("║ 🔔 BUZZER (GPIO ");
  Serial.print(PIN_ALARMA);
  Serial.print(")    : ");
  Serial.println(alertaDeslizamiento ? "🔊 SONANDO [HIGH]" : "🔇 SILENCIO [LOW]");
  Serial.println("╚════════════════════════════════════════════════════════════════╝");
}

void setup() {
  Serial.begin(115200);
  delay(400);
  pinMode(PIN_ALARMA, OUTPUT);
  activarBuzzer(false);

  // Prueba rápida de 300 ms al encender para verificar el buzzer activo
  activarBuzzer(true);
  delay(300);
  activarBuzzer(false);

  Serial.println();
  Serial.println("╔════════════════════════════════════════════════════════════════╗");
  Serial.println("║     SISTEMA DE ALERTA TEMPRANA DE DESLIZAMIENTOS (EDGE AI)     ║");
  Serial.println("║         Proyecto Integrador - Nodo ESP32-S3 (Garagoa)          ║");
  Serial.println("╚════════════════════════════════════════════════════════════════╝");

  // Iniciar I2C en pines de ESP32-S3 (SDA = 8, SCL = 9) y correr escáner
  Wire.begin(8, 9);
  ejecutarEscanerI2C();

  // Despertar MPU6050 en dirección confirmada 0x69
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); // Registro PWR_MGMT_1
  Wire.write(0);    // Despertar MPU6050
  Wire.endTransmission(true);

  // Cargar modelo cuantizado desde model.h
  tfl_model = tflite::GetModel(g_model);
  static tflite::MicroMutableOpResolver<3> resolver;
  resolver.AddFullyConnected();
  resolver.AddRelu();
  resolver.AddSoftmax();

  static tflite::MicroInterpreter static_interpreter(
    tfl_model, resolver, tensor_arena, kTensorArenaSize, &micro_error_reporter
  );
  interpreter = &static_interpreter;
  interpreter->AllocateTensors();

  input = interpreter->input(0);
  output = interpreter->output(0);

  Serial.println("┌────────────────────────────────────────────────────────────────┐");
  Serial.println("│ ⚙️  ESPECIFICACIONES Y RECURSOS EN EJECUCIÓN                   │");
  Serial.println("├────────────────────────────────────────────────────────────────┤");
  Serial.print("│  • Tensor Arena SRAM : ");
  Serial.print(interpreter->arena_used_bytes());
  Serial.print(" / ");
  Serial.print(kTensorArenaSize);
  Serial.println(" bytes reservados               │");
  Serial.println("│  • Frecuencia Muestreo : 20 Hz (cada 50 ms) | FIFO: 50 datos   │");
  Serial.println("│  • Filtro Acelerómetro : Media Móvil W = 5 | Salidas ML: 3     │");
  Serial.println("└────────────────────────────────────────────────────────────────┘");
}

void loop() {
  unsigned long tiempoActual = millis();

  if (tiempoActual - tiempoAnterior >= INTERVALO_MS) {
    tiempoAnterior = tiempoActual;

    // 1. Adquisición física de sensores (MPU6050 en 0x69 + Humedad ADC)
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x3B); // ACCEL_XOUT_H
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_ADDR, 6, true);

    if (Wire.available() < 6) return; // Protección contra lectura I2C incompleta

    int16_t raw_ax = Wire.read() << 8 | Wire.read();
    int16_t raw_ay = Wire.read() << 8 | Wire.read();
    int16_t raw_az = Wire.read() << 8 | Wire.read();

    MuestraSensor lectura;
    lectura.ax = raw_ax / 16384.0f;
    lectura.ay = raw_ay / 16384.0f;
    lectura.az = raw_az / 16384.0f;
    lectura.humedad_adc = analogRead(PIN_HUMEDAD);

    ventanaEdge.push(lectura);

    // 2. Humedad, inclinación suavizada con filtro W=5 (Pitch) y cambio (dPitch)
    float hum_bruto_pct = (lectura.humedad_adc / 4095.0f) * 100.0f;
    float hum_pct = constrain(100.0f - hum_bruto_pct, 0.0f, 100.0f);
    float hum_fifo_pct = ventanaEdge.promedioHumedadPct();

    float ax_f, ay_f, az_f;
    ventanaEdge.promedioAceleracionW(5, ax_f, ay_f, az_f);

    float pitch = atan2(
      ax_f,
      sqrt(ay_f * ay_f + az_f * az_f)
    ) * 180.0f / PI;

    static float pitch_anterior = pitch;
    float vel_inclinacion = pitch - pitch_anterior;
    pitch_anterior = pitch;

    // Normalización Z-Score y cuantización a int8 para entrada del modelo
    float features[3] = {hum_pct, pitch, vel_inclinacion};
    for (int i = 0; i < 3; i++) {
      float norm = (features[i] - X_MEAN[i]) / X_STD[i];
      int cuant = (int)round(norm / input->params.scale + input->params.zero_point);
      input->data.int8[i] = (int8_t)constrain(cuant, -128, 127);
    }

    // 3. Inferencia TinyML + Dequantización de las 3 clases + Latencia (N = 100)
    unsigned long t_inicio = micros();
    interpreter->Invoke();
    unsigned long t_fin = micros();
    float ultima_latencia_ms = (t_fin - t_inicio) / 1000.0f;

    float probs[3];
    int clase_predicha = 0;
    for (int c = 0; c < 3; c++) {
      int8_t raw_c = output->data.int8[c];
      probs[c] = constrain((raw_c - output->params.zero_point) * output->params.scale, 0.0f, 1.0f);
      if (probs[c] > probs[clase_predicha]) {
        clase_predicha = c;
      }
    }

    int8_t ml_raw = output->data.int8[clase_predicha];
    float ml_prob = probs[2]; // Probabilidad de Clase 2 (Deslizamiento inminente)

    if (indice_prueba < N_PRUEBAS) {
      tiempos_ms[indice_prueba] = ultima_latencia_ms;
      indice_prueba++;

      if (indice_prueba == N_PRUEBAS) {
        float suma = 0.0f;
        for (int i = 0; i < N_PRUEBAS; i++) suma += tiempos_ms[i];
        float promedio = suma / N_PRUEBAS;

        float suma_var = 0.0f;
        for (int i = 0; i < N_PRUEBAS; i++) {
          float diff = tiempos_ms[i] - promedio;
          suma_var += diff * diff;
        }
        float desviacion = sqrt(suma_var / N_PRUEBAS);

        Serial.println();
        Serial.println("╔════════════════════════════════════════════════════════════════╗");
        Serial.println("║ 📊 BENCHMARK OFICIAL DE LATENCIA TINYML (N = 100 MUESTRAS)     ║");
        Serial.print("║    Latencia Promedio : ");
        Serial.print(promedio, 3);
        Serial.print(" ± ");
        Serial.print(desviacion, 3);
        Serial.println(" ms                         ║");
        Serial.println("╚════════════════════════════════════════════════════════════════╝");
      }
    }

    // 4. Lógica de alerta híbrida: Clasificación TinyML + respaldo físico
    bool alertaHumedad = (hum_pct > UMBRAL_HUMEDAD_MIN_PCT);
    bool hayVibracion = (fabs(vel_inclinacion) >= UMBRAL_VIBRACION_DEG);
    bool alertaIA = (clase_predicha == 2 || probs[2] >= 0.60f);
    bool alertaDeslizamiento = (alertaIA || (alertaHumedad && hayVibracion));
    activarBuzzer(alertaDeslizamiento);

    // 5. Impresión ordenada en Monitor Serial cada 1.2 s
    if (tiempoActual - ultimoPrintMs >= INTERVALO_PRINT_MS) {
      ultimoPrintMs = tiempoActual;
      contadorTarjetas++;

      imprimirTarjetaMonitor(
        contadorTarjetas,
        tiempoActual,
        lectura,
        hum_pct,
        hum_fifo_pct,
        pitch,
        vel_inclinacion,
        hayVibracion,
        alertaHumedad,
        alertaIA,
        clase_predicha,
        alertaDeslizamiento,
        ml_raw,
        ml_prob,
        ultima_latencia_ms
      );
    }
  }
}
