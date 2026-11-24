import os
import requests
import pandas as pd
from datetime import datetime

# Configurar Pandas para mostrar todas las columnas sin recortar con '...'
pd.set_option("display.max_columns", None)
pd.set_option("display.max_rows", 100)
pd.set_option("display.width", 1000)

# Coordenadas geográficas de Garagoa, Boyacá (Valle de Tenza)
LATITUD = 5.08
LONGITUD = -73.36
NODO_ID = "Garagoa_Nodo_1"

URL_OPEN_METEO = (
    f"https://api.open-meteo.com/v1/forecast?"
    f"latitude={LATITUD}&longitude={LONGITUD}"
    f"&current=temperature_2m,relative_humidity_2m,precipitation"
    f"&hourly=temperature_2m,relative_humidity_2m,precipitation"
    f"&past_days=7&forecast_days=1&timezone=America%2FBogota"
)

def ejecutar_etl_garagoa():
    print("=" * 78)
    print("  SISTEMA ETL CONTEXTUAL Y SUPERVISIÓN MACRO-CLIMÁTICA (GARAGOA - BOYACÁ)")
    print("=" * 78)
    print(f"Conectando con Open-Meteo (Lat: {LATITUD}, Lon: {LONGITUD})...\n")

    respuesta = requests.get(URL_OPEN_METEO, timeout=15)
    respuesta.raise_for_status()
    datos = respuesta.json()

    # 1. Transformación del historial horario (últimos 7 días)
    hourly = datos["hourly"]
    df = pd.DataFrame({
        "fecha_hora": pd.to_datetime(hourly["time"]),
        "temperatura_C": hourly["temperature_2m"],
        "humedad_relativa_%": hourly["relative_humidity_2m"],
        "lluvia_instantanea_mm": hourly["precipitation"],
    })

    # Filtrar registros hasta la hora actual
    ahora = pd.Timestamp.now()
    df = df[df["fecha_hora"] <= ahora].copy()
    df["lluvia_acumulada_24h_mm"] = (
        df["lluvia_instantanea_mm"].rolling(window=24, min_periods=1).sum().round(2)
    )
    df["fecha_hora"] = df["fecha_hora"].dt.strftime("%Y-%m-%d %H:%M")

    # 2. Estructura de Datos O(1): Tabla Hash indexada por ID de estación
    actual = datos["current"]
    lluvia_24h_actual = float(df["lluvia_acumulada_24h_mm"].iloc[-1])

    tabla_hash_clima = {
        NODO_ID: {
            "fecha_hora": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "temperatura_C": actual["temperature_2m"],
            "humedad_relativa_%": actual["relative_humidity_2m"],
            "lluvia_instantanea_mm": actual["precipitation"],
            "lluvia_acumulada_24h_mm": lluvia_24h_actual,
        }
    }

    # Consulta en tiempo constante O(1)
    telemetria = tabla_hash_clima[NODO_ID]

    print(f"--- CONSULTA EN TABLA HASH O(1) | CLAVE: '{NODO_ID}' ---")
    print(f"  • Timestamp consulta      : {telemetria['fecha_hora']}")
    print(f"  • Temperatura ambiente    : {telemetria['temperatura_C']} °C")
    print(f"  • Humedad relativa aire   : {telemetria['humedad_relativa_%']} %")
    print(f"  • Lluvia instantánea      : {telemetria['lluvia_instantanea_mm']} mm")
    print(f"  • Lluvia acumulada (24h)  : {telemetria['lluvia_acumulada_24h_mm']} mm")

    # 3. Diagnóstico contextual de saturación hídrica
    print("\n--- DIAGNÓSTICO CONTEXTUAL DE RIESGO ---")
    if telemetria["lluvia_acumulada_24h_mm"] >= 15.0:
        print("  [CRÍTICO] Lluvia acumulada 24h >= 15 mm: Alta probabilidad de saturación.")
    elif telemetria["lluvia_acumulada_24h_mm"] > 0.5 or telemetria["humedad_relativa_%"] >= 65:
        print("  [PREVENTIVO] Precipitación reciente o humedad alta en Garagoa.")
        print("               Posible pérdida gradual de fricción en la ladera.")
    else:
        print("  [ESTABLE] Condiciones atmosféricas secas en las últimas 24 horas.")

    # 4. Guardar CSV en 04_Datasets si existe la carpeta, o en la ruta actual
    ruta_salida = (
        os.path.join("04_Datasets", "historico_clima_garagoa.csv")
        if os.path.isdir("04_Datasets")
        else "historico_clima_garagoa.csv"
    )
    df.to_csv(ruta_salida, index=False)

    # 5. Resumen estadístico y visualización de las últimas 65 líneas
    print("\n--- RESUMEN VENTANA HISTÓRICA (7 DÍAS) ---")
    print(f"  • Total registros horarios : {len(df)} filas")
    print(f"  • Rango de temperatura     : {df['temperatura_C'].min():.1f} °C a {df['temperatura_C'].max():.1f} °C")
    print(f"  • Máximo acumulado 24h     : {df['lluvia_acumulada_24h_mm'].max():.2f} mm")
    print(f"  • Archivo exportado        : {ruta_salida}")

    print("\n" + "=" * 78)
    print("  ÚLTIMAS 65 OBSERVACIONES HORARIAS REGISTRADAS EN GARAGOA")
    print("=" * 78)
    print(df.tail(65).to_string(index=False))
    print("=" * 78)

if __name__ == "__main__":
    ejecutar_etl_garagoa()
