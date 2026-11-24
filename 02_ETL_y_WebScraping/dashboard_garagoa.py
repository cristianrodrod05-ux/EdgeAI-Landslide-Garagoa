# -*- coding: utf-8 -*-
"""
Módulo de Supervisión Climática y Tabla Hash O(1) - Proyecto Garagoa
Extrae el historial horario de los últimos 7 días y el estado vigente desde
la API de Open-Meteo para contrastar la saturación del suelo en el Valle de Tenza.
"""

from datetime import datetime
import pandas as pd
import requests

# Coordenadas de Garagoa, Boyacá (Valle de Tenza)
URL_OPEN_METEO = (
    "https://api.open-meteo.com/v1/forecast?"
    "latitude=5.08&longitude=-73.36"
    "&current=temperature_2m,precipitation,relative_humidity_2m"
    "&hourly=temperature_2m,relative_humidity_2m,precipitation"
    "&past_days=7&forecast_days=1"
    "&timezone=America%2FBogota"
)


def consultar_clima_garagoa(archivo_salida: str = "historico_clima_garagoa.csv"):
    print("Conectando con Open-Meteo para extraer historial climático de Garagoa (últimos 7 días)...\n")

    try:
        respuesta = requests.get(URL_OPEN_METEO, timeout=10)
        respuesta.raise_for_status()
        datos = respuesta.json()

        # 1. Construir serie histórica horaria real (cada 1 hora) hasta el instante actual
        df_horario = pd.DataFrame({
            "fecha_hora": datos["hourly"]["time"],
            "temperatura_C": datos["hourly"]["temperature_2m"],
            "humedad_relativa_%": datos["hourly"]["relative_humidity_2m"],
            "lluvia_instantanea_mm": datos["hourly"]["precipitation"],
        })

        # Filtrar únicamente horas ya ocurridas (excluir pronóstico futuro del día)
        hora_actual_api = datos["current"]["time"]
        df_horario = df_horario[df_horario["fecha_hora"] <= hora_actual_api].copy()

        # Calcular lluvia acumulada móvil de las últimas 24 horas para cada registro
        df_horario["lluvia_acumulada_24h_mm"] = (
            df_horario["lluvia_instantanea_mm"].rolling(window=24, min_periods=1).sum().round(2)
        )
        df_horario["fecha_hora"] = df_horario["fecha_hora"].str.replace("T", " ")

        # 2. ESTRUCTURA DE DATOS: Tabla Hash (dict) para acceso O(1)
        ultimo_acumulado_24h = float(df_horario["lluvia_acumulada_24h_mm"].iloc[-1])
        tabla_hash_clima = {
            "Garagoa_Nodo_1": {
                "fecha_hora": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                "temperatura_C": datos["current"]["temperature_2m"],
                "humedad_relativa_%": datos["current"]["relative_humidity_2m"],
                "lluvia_instantanea_mm": datos["current"]["precipitation"],
                "lluvia_acumulada_24h_mm": ultimo_acumulado_24h,
            }
        }

        # 3. Consulta rápida O(1) y validación cruzada macro-climática
        registro_actual = tabla_hash_clima["Garagoa_Nodo_1"]
        print("=== DASHBOARD CONTEXTUAL: PREVENCIÓN DE DESLIZAMIENTOS ===")
        print("Búsqueda rápida en Tabla Hash O(1) arrojó los siguientes resultados:")
        print(registro_actual)

        if (
            registro_actual["lluvia_instantanea_mm"] > 0
            or registro_actual["lluvia_acumulada_24h_mm"] > 0
            or registro_actual["humedad_relativa_%"] > 85
        ):
            print("\n⚠️ ALERTA MACRO-CLIMÁTICA: Precipitación reciente o humedad alta en Garagoa.")
            print("El suelo puede presentar saturación hídrica y pérdida de fricción en la ladera.")
        else:
            print("\n✅ Clima externo seco. Sin precipitaciones registradas en las últimas 24 horas.")

        # 4. Exportar dataset extendido a CSV
        df_horario.to_csv(archivo_salida, index=False)
        print(f"\n=== DATASET CLIMÁTICO GUARDADO ({len(df_horario)} registros horarios reales) ===")
        print(df_horario.tail(10))

    except requests.RequestException as error:
        print(f"⚠️ Error de conexión con Open-Meteo: {error}")


if __name__ == "__main__":
    consultar_clima_garagoa()
