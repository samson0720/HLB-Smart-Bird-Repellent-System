# HLB-Smart-Bird-Repellent-System
An Edge AI &amp; IoT based active bird deterrent system for wind turbines.

# 🦅 HLB 智慧驅鳥系統 (Smart Bird Repellent System)

> **2026 智慧創新大賞 (Best AI Awards) 參賽專案**
> 
> 本專案旨在解決風力發電場中候鳥撞擊風機的生態與設備維運痛點。透過導入「雲邊協同」架構，結合 RTL8735B 邊緣視覺 AI、軌跡預測演算法與超聲波測距，實現兼顧綠能發展與 ESG 生態永續的主動式智慧防護系統。

## ✨ 核心技術亮點 (Key Features)

* **Edge AI 模型輕量化 (TinyML)**：使用 Roboflow 進行鳥種細粒度標註，並將 YOLO v4-Tiny 模型量化壓縮，部署於 HUB-8735_ultra 模組，實現低延遲即時推論。
* **動態軌跡預測**：透過連續影格計算目標移動向量，精準判斷碰撞風險，避免「安全路過」造成的無謂警報。
* **雲邊協同與多重感測**：後端介接氣象 API 動態調整辨識靈敏度；前端結合超聲波感測器 (HC-SR04) 建立「聲波驅離 ➔ 強制停機」的雙層分級防護。
* **IoT 戰情室與即時通報**：設備狀態即時同步至遠端 Web Dashboard，並於觸發危險警報時透過 LINE Notify 進行推播。

## 📂 專案架構 (Repository Structure)

本專案整合了硬體控制、邊緣 AI 模型與 Web 平台，程式碼目錄結構如下：

* `/Hardware_Edge/`：包含 Raspberry Pi Pico 2W 的控制邏輯、超聲波感測與蜂鳴器硬體作動程式碼 (C/C++ / MicroPython)。
* `/AI_Model/`：包含 YOLO v4-Tiny 模型訓練腳本、量化轉換設定，以及 RTL8735B 的視覺推論邏輯。
* `/Web_Dashboard/`：包含後台伺服器、前端戰情室網頁與 LINE Notify 串接 API 程式碼 (Python/FastAPI / HTML/JS)。
* `/Docs/`：系統架構圖與專案相關說明文件。

## 🛠️ 開發環境與硬體設備

* **核心控制中樞**：Raspberry Pi Pico 2W
* **AI 視覺邊緣模組**：AmebaPro2 HUB-8735_ultra (RTL8735B)
* **感測與作動元件**：HC-SR04 超聲波感測器、WS2812B 狀態指示燈條、蜂鳴器
* **軟體與開源工具**：YOLO v4-Tiny, Roboflow, AmebaPro AI 轉換工具
