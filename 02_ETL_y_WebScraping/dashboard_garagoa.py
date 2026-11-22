import time
import math
import requests
import serial

# =========================================================================
# CONFIGURACIÓN (Cambia a False cuando conectes el ESP32 físico por USB)
# =========================================================================
MODO_PRUEBA_SIN_PLACA = True
PUERTO_COM = "COM3"
BAUDIOS = 115200

# 1. MÓDULO DE WEB SCRAPING + TABLA HASH O(1) (Requisitos R5 y R6)
def obtener_clima_garagoa():
    url = "https://api.open-meteo.com/v1/forecast?latitude=5.08&longitude=-73.36&current=temperature_2m,precipitation,relative_humidity_2m&hourly=precipitation&past_days=1&forecast_days=1&timezone=America%2FBogota"
    respuesta = requests.get(url, timeout=10)
    datos = respuesta.json()
    
    # Suma de lluvia acumulada en las últimas 24 horas
    lluvia_24h = round(sum(datos['hourly']['precipitation'][:24]), 2)
    
    # Estructura de Datos: Tabla Hash (Diccionario en Python)
    tabla_hash = {
        "Garagoa_Nodo_1": {
            "temperatura_C": datos['current']['temperature_2m'],
            "humedad_aire_%": datos['current']['relative_humidity_2m'],
            "lluvia_actual_mm": datos['current']['precipitation'],
            "lluvia_acumulada_24h_mm": lluvia_24h
        }
    }
    return tabla_hash

print("Conectando con Open-Meteo para actualizar Tabla Hash de Garagoa...")
tabla_hash_clima = obtener_clima_garagoa()
clima_zona = tabla_hash_clima["Garagoa_Nodo_1"]  # Consulta en tiempo O(1)

# 2. LECTURA DEL NODO EDGE Y VALIDACIÓN CRUZADA
if not MODO_PRUEBA_SIN_PLACA:
    esp32 = serial.Serial(PUERTO_COM, BAUDIOS, timeout=1)
    time.sleep(2)

print("\n" + "=" * 68)
print("     DASHBOARD CONTEXTUAL: PREVENCIÓN DE DESLIZAMIENTOS (GARAGOA)     ")
print("=" * 68)
print(f"[WEB SCRAPING - HASH O(1)] Temp: {clima_zona['temperatura_C']}°C | "
      f"Hum. Aire: {clima_zona['humedad_aire_%']}% | Lluvia 24h: {clima_zona['lluvia_acumulada_24h_mm']} mm")
print("-" * 68)

# Monitoreo de 5 ciclos (cuando hagan la demo en vivo pueden cambiar range(1, 6) por range(1, 50))
for ciclo in range(1, 6):
    if MODO_PRUEBA_SIN_PLACA:
        # Simula una trama enviada por el ESP32: ax, ay, az, humedad_adc
        ax, ay, az, humedad_adc = 0.52, 0.03, 0.85, 3450
    else:
        linea = esp32.readline().decode('utf-8', errors='ignore').strip()
        partes = linea.split(",")
        if len(partes) != 4:
            continue
        ax, ay, az, humedad_adc = float(partes[0]), float(partes[1]), float(partes[2]), int(partes[3])

    # Transformación física rápida
    humedad_suelo_pct = round((humedad_adc / 4095.0) * 100.0, 1)
    pitch = round(math.degrees(math.atan2(ax, math.sqrt(ay**2 + az**2))), 1)

    # Cruce de información: Sensor interno vs. Clima externo de Garagoa
    lluvia_detectada = (clima_zona["lluvia_actual_mm"] > 0) or (clima_zona["lluvia_acumulada_24h_mm"] > 1.0) or (clima_zona["humedad_aire_%"] > 85)

    if humedad_suelo_pct > 75.0 and lluvia_detectada:
        diagnostico = "ALERTA CRÍTICA CONFIRMADA (Suelo saturado + Lluvia en la zona)"
    elif humedad_suelo_pct > 75.0 and not lluvia_detectada:
        diagnostico = "ADVERTENCIA LOCAL (Suelo húmedo sin lluvia externa - Posible fuga)"
    else:
        diagnostico = "NORMAL (Ladera estable)"

    print(f"Lectura {ciclo} | Suelo: {humedad_suelo_pct}% | Ángulo: {pitch}° -> {diagnostico}")
    time.sleep(1)

if not MODO_PRUEBA_SIN_PLACA:
    esp32.close()
print("=" * 68)
