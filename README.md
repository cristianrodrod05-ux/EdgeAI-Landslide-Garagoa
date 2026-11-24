# Edge AI Early Warning System for Landslides (Valle de Tenza - Garagoa)

Autonomous, low-cost embedded system designed to detect and classify imminent rainfall-induced landslides locally on an **ESP32** microcontroller using **TinyML (TensorFlow Lite Micro)**, a custom **ETL** acquisition pipeline, and deterministic **Data Structures**.

## Team Members (Universidad Santo Tomás - Tunja)
* **Cristian Andrés Rodríguez R.** — ETL Pipeline, Contextual Web Scraping & TinyML Quantization & Neural Network Architecture 
* **Diego Alejandro Rivera P.** — Embedded C++ Firmware, I2C Sensors & Circular Buffer &  Mechanical Physics Modeling
* **Fabian Ricardo Fuya C.** — Physical Prototype & Validation Metrics
* **Juan José Sabogal S.** — IEEE Paper


## Repository Structure
* `01_Firmware_ESP32/`: C++ source code (`nodo_garagoa.ino`) implementing a static $O(1)$ FIFO Circular Buffer and quantized model header (`model.h`, 3.39 KB).
* `02_ETL_y_WebScraping/`: Python scripts for real-time UART/Serial extraction, moving-average noise filtering, pitch angle trigonometry (`Captura_real_ETL.py`), and Open-Meteo API scraping with an $O(1)$ Hash Table.
* `03_Modelo_TinyML/`: TensorFlow/Keras training and INT8 post-training quantization notebook (`Entrenamiento_Final.ipynb`).
* `04_Datasets/`: Proprietary labeled geotechnical datasets (`dataset_ladera_v1.csv`, `dataset_real_garagoa.csv`) and historical precipitation logs (`historico_clima_garagoa.csv`).

## Hardware Pinout (ESP32)
| Component | Protocol / Signal | ESP32 Pin |
| :--- | :--- | :--- |
| **MPU6050 (IMU)** | I2C (`SDA` / `SCL`) | `GPIO 21` / `GPIO 22` |
| **Soil Moisture Sensor** | Analog (12-bit ADC) | `GPIO 34` |
| **Local Alarm (LED/Buzzer)** | Digital Output | `GPIO 2` |
