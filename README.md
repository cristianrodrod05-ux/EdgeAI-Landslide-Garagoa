# Edge AI Early Warning System for Landslides (Valle de Tenza - Garagoa)

Autonomous, low-cost embedded system designed to detect and classify imminent rainfall-induced landslides locally on an **ESP32-S3** microcontroller using **TinyML (TensorFlow Lite Micro)**, a custom **ETL** acquisition pipeline, and deterministic **Data Structures**.

## Team Members (Universidad Santo Tomás - Tunja)
- **Cristian Andrés Rodríguez R.** — ETL Pipeline, Contextual Web Scraping , TinyML Quantization & Neural Network Architecture 
- **Diego Alejandro Rivera P.** — Embedded C++ Firmware, I2C Sensors , Circular Buffer &  Mechanical Physics Modeling
- **Fabian Ricardo Fuya C.** — Physical Prototype & Validation Metrics
- **Juan José Sabogal S.** — IEEE Paper

## System Architecture & Data Structures
1. **Edge Acquisition & $\mathcal{O}(1)$ FIFO Circular Buffer:** Samples soil moisture and 3-axis acceleration at **20 Hz** (every 50 ms) inside a static circular buffer (`WINDOW_SIZE = 50`), applying a moving-average noise filter ($W = 5$) to compute slope inclination ($\text{Pitch}$) and angular velocity ($\Delta\text{Pitch}$) in constant time.
2. **Contextual Macro-Climatic ETL & $\mathcal{O}(1)$ Hash Table:** Queries the Open-Meteo REST API for **Garagoa, Boyacá** (`lat=5.08, lon=-73.36`), indexing real-time weather and 24-hour rolling precipitation in a Python dictionary (`"Garagoa_Nodo_1"`) for $\mathcal{O}(1)$ cross-validation.
3. **On-Device TinyML Inference (INT8):** Runs a quantized Multi-Layer Perceptron (`3 → 16 → 8 → 3`) on TensorFlow Lite Micro to classify geotechnical risk locally without cloud dependency.

## Repository Structure
- `01_Firmware_ESP32/`: Embedded C++ firmware (`nodo_garagoa.ino`) implementing the static $\mathcal{O}(1)$ FIFO Circular Buffer, Z-score normalization, local buzzer alert logic, and the quantized INT8 model header (`model.h`, 3,472 bytes / 3.39 KB).
- `02_ETL_y_WebScraping/`: Python ETL and contextual weather supervision scripts (`dashboard_garagoa.py` and executed notebook `Scraping_clima_Garagoa.ipynb`) that query Open-Meteo, structure node telemetry in an $\mathcal{O}(1)$ Hash Table, and export hourly rainfall logs.
- `03_Modelo_TinyML/`: TensorFlow/Keras training, stratified splitting (`70% / 15% / 15%`), and INT8 post-training quantization notebook (`Entrenamiento_Final.ipynb`).
- `04_Datasets/`:
  - `dataset_ladera_v1.csv`: **500 laboratory-calibrated physical-synthetic geotechnical samples** generated across the empirical operating ranges characterized on the inclined MDF terrain prototype (soil moisture `28%–93%`, pitch angle `24°–43°`, and angular velocity) balanced across 3 risk classes (`0`: Stable, `1`: Preventive Saturation, `2`: Imminent Landslide).
  - `historico_clima_garagoa.csv`: **181 real hourly meteorological records** extracted from Open-Meteo for Garagoa (Nov 17–24, 2026), including temperature, relative humidity, instantaneous precipitation, and 24-hour rolling accumulated rainfall.

## Hardware Pinout (ESP32-S3)
| Component | Protocol / Signal | ESP32 Pin |
| :--- | :--- | :--- |
| **MPU6050 (IMU)** | I2C (`SDA` / `SCL`) | `GPIO 8` / `GPIO 9` |
| **Soil Moisture Sensor** | Analog (12-bit ADC) | `GPIO 4` |
| **Local Alarm (LED/Buzzer)** | Digital Output | `GPIO 2` |

## TinyML Model & Edge Specifications
| Metric / Parameter | Value |
| :--- | :--- |
| **Model Architecture** | MLP (`Input(3) → Dense(16, ReLU) → Dense(8, ReLU) → Dense(3, Softmax)`) |
| **Original Model (Float32)** | Accuracy: `94.67%` \| Weighted F1-Score: `94.72%` |
| **Quantized Model (INT8)** | Accuracy: `94.67%` \| Weighted F1-Score: `94.72%` |
| **Flash Footprint (`model.h`)** | `3,472 bytes` (`3.39 KB`) |
| **Reserved Tensor Arena (SRAM)** | `4 KB` (`4,096 bytes`) |
| **Sampling Frequency** | `20 Hz` (`50 ms` period) |

## Quick Start
1. **Run Contextual Weather ETL & Hash Table Supervision:**
   ```bash
   pip install requests pandas
   python 02_ETL_y_WebScraping/dashboard_garagoa.py
   
