# -*- coding: utf-8 -*-
"""
Módulo de Supervisión Climática y Tabla Hash O(1) - Proyecto Garagoa
Consulta la API de Open-Meteo para contrastar la saturación del suelo
con las precipitaciones reales de la región del Valle de Tenza.
"""

import time
from datetime import datetime
import pandas as pd
import requests

# Coordenadas de Garagoa, Boyacá (Valle de Tenza)
URL_OPEN_METEO = (
    "https://api.open-meteo.com/v1/forecast?"
    "latitude=5.08&longitude=-73.36"
    "&current=temperature_2m,precipitation,relative_humidity_2m"
    "&hourly=precipitation&past_days=1&forecast_days=1"
    "&timezone=America%2FBogota"
)


def consultar_clima_garagoa(num_extracciones: int = 3, espera_seg: int = 2):
    historial_registros = []
    tabla_hash_clima = {}

    print("Conectando con Open-Meteo para extraer datos de Garagoa...\n")

    for intento in range(num_extracciones):
        print(f"Realizando extracción #{intento + 1}...")
        try:
            respuesta = requests.get(URL_OPEN_METEO, timeout=10)
            respuesta.raise_for_status()
            datos = respuesta.json()

            # Lluvia acumulada en 24 horas
            lluvias_horarias = datos["hourly"]["precipitation"][:24]
            lluvia_acumulada_24h = round(sum(lluvias_horarias), 2)

            # ESTRUCTURA DE DATOS: Tabla Hash (dict) para acceso O(1)
            tabla_hash_clima["Garagoa_Nodo_1"] = {
                "fecha_hora": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                "temperatura_C": datos["current"]["temperature_2m"],
                "humedad_relativa_%": datos["current"]["relative_humidity_2m"],
                "lluvia_instantanea_mm": datos["current"]["precipitation"],
                "lluvia_acumulada_24h_mm": lluvia_acumulada_24h,
            }

            historial_registros.append(tabla_hash_clima["Garagoa_Nodo_1"].copy())

        except requests.RequestException as error:
            print(f"⚠️ Error de conexión en extracción #{intento + 1}: {error}")

        if intento < num_extracciones - 1:
            time.sleep(espera_seg)

    if not historial_registros:
        print("\n✖ No se pudieron obtener datos climáticos. Revisa la conexión a internet.")
        return

    # Consulta rápida O(1) en la Tabla Hash
    registro_actual = tabla_hash_clima["Garagoa_Nodo_1"]
    print("\n=== DASHBOARD CONTEXTUAL: PREVENCIÓN DE DESLIZAMIENTOS ===")
    print("Búsqueda rápida en Tabla Hash O(1) arrojó los siguientes resultados:")
    print(registro_actual)

    # Lógica de validación cruzada macro-climática
    lluvia_inst = registro_actual["lluvia_instantanea_mm"]
    lluvia_24h = registro_actual["lluvia_acumulada_24h_mm"]
    humedad_rel = registro_actual["humedad_relativa_%"]

    if lluvia_inst > 0 or lluvia_24h > 0 or humedad_rel > 85:
        print("\n⚠️ ALERTA MACRO-CLIMÁTICA: Precipitación reciente o humedad alta en Garagoa.")
        print("El suelo puede presentar saturación hídrica y pérdida de fricción en la ladera.")
    else:
        print("\n✅ Clima externo seco. Sin precipitaciones registradas en las últimas 24 horas.")

    # Exportar historial acumulado a CSV
    df_clima = pd.DataFrame(historial_registros)
    df_clima.to_csv("historico_clima_garagoa.csv", index=False)
    print("\n=== DATASET CLIMÁTICO ACUMULADO GUARDADO (historico_clima_garagoa.csv) ===")
    print(df_clima)


if __name__ == "__main__":
    consultar_clima_garagoa()
