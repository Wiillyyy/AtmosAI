# AtmosAI
A small 3rd year network and telecom project.

## Overview
AtmosAI is an Edge AI-powered weather station designed to predict local meteorological conditions autonomously. Instead of relying on cloud computing, this project executes neural network inference directly on the edge using an STM32 microcontroller equipped with a Neural Processing Unit (NPU).

### This project is developed by a team of three 3rd-year undergraduate students in Telecommunications, Networks, and Computer Science (L3 TRI).
* William Z.
* Mostapha K.
* Franck G.


## Hardware Specifications
* **Microcontroller**: NUCLEO-N657X0 (ARM Cortex-M33) / NUCLEO-F446RE / Shield X-NUCLEO-IKS01A3.
* **AI Acceleration**: Integrated Neural-ART NPU (600 GOPS) optimized for deep learning inference
* **Sensors**: Temperature, Humidity, Pressure, and GPS.

## Software Stack
* **Machine Learning**: Python, TensorFlow/Keras for offline training.
* **Edge AI Deployment**: X-CUBE-AI / STM32Cube.AI for model optimization, quantization, and C code generation.
* **Firmware**: C/C++ compiled via STM32CubeIDE.

## Architecture
1.  **Data Acquisition**: Reading raw environmental data from sensors via I2C and UART interfaces.
2.  **Preprocessing**: Data cleaning, normalization, and time-series formatting.
3.  **Inference**: Executing the optimized predictive neural network model locally on the STM32 NPU.
4.  **Output**: Displaying local weather predictions, such as precipitation probability or temperature trends.

## Academic Context
Developed as part of the "Embedded Systems and Edge AI" module, the objective is to evaluate the sustainability, latency, and energy consumption of embedded AI compared to classical deterministic methods.

## 👤 Auteur

William / Franck / Mostapha — L3 Réseaux & Télécoms  
Module ETRS606 — IA Embarquée  
Université Savoie Mont Blanc