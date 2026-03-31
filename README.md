# HLB — 智慧驅鳥防護系統

> **2026 智慧創新大賞 (Best AI Awards) 參賽作品**
>
> HLB 是一套以 Edge AI 與 IoT 為核心的主動式風機驅鳥系統，結合輕量化目標偵測（YOLOv4-Tiny）、動態軌跡追蹤、超聲波近端防護與即時 Web Dashboard，在候鳥接近風力發電機前主動發出警示，兼顧綠能發展與 ESG 生態永續。

---

## 目錄

- [專案目的](#專案目的)
- [核心功能](#核心功能)
- [系統架構](#系統架構)
- [技術棧](#技術棧)
- [硬體清單](#硬體清單)
- [專案結構](#專案結構)
- [安裝與部署](#安裝與部署)
- [API 端點說明](#api-端點說明)
- [環境變數設定](#環境變數設定)

---

## 專案目的

風力發電機的葉片每分鐘高速旋轉，候鳥極難即時察覺，撞擊事故每年造成大量鳥類傷亡，也影響設備運維成本。

本專案目標是在**邊緣端（無需雲端推論）**完成候鳥偵測與軌跡預測，當鳥類飛行路徑指向危險區時，立即觸發聲波驅離；超聲波感測器則作為近端第二道防線，雙層分級保護同時降低生態影響與維修成本。

---

## 核心功能

### 1. 邊緣 AI 鳥類偵測
- 以 YOLOv4-Tiny 即時辨識畫面中的候鳥（目前支援**鴿子**與**黑腹濱鷸**）。
- 模型直接部署於 AmebaPro2 (RTL8735B) 模組，無需連線遠端伺服器，推論延遲低於 100ms。
- 偵測結果以綠色邊界框疊加於 RTSP 串流畫面。

### 2. 動態軌跡追蹤與碰撞預測
- 最多同時追蹤 **3 隻**獨立目標，每隻保留 **8 幀**歷史位置。
- 以移動向量計算飛行方向，在畫面上繪製**漸層黃色軌跡線**與**預測箭頭**。
- 若箭頭指向預設的 **TURBINE ZONE** 危險區，即觸發警報。

### 3. 分級聲波驅離
- **第一層（AI 視覺）**：RTL8735B 偵測到危險軌跡 → 蜂鳴器啟動 + OSD 疊加 `!!! DANGER !!!`。
- **第二層（超聲波近端）**：HC-SR04 偵測到物體距離 < 30 cm → 紅燈亮起、綠燈熄滅，提供額外近端警示。

### 4. 即時 RTSP 視訊串流與 OSD 疊加
- FHD (1920×1080) H.264 串流，碼率 2 Mbps。
- 畫面疊加：危險區邊界框、每隻鳥的軌跡歷史、預測方向箭頭、DANGER 警示文字。
- 可透過 VLC 或配合 `rtsp-to-websocket-server.js` 在瀏覽器中觀看。

### 5. 超聲波近端感測 HTTP API
- Raspberry Pi Pico W 持續以 10Hz 讀取 HC-SR04 距離數據。
- 提供 `/api/data` JSON 端點，Dashboard 或其他服務可直接串接。
- 內建簡易網頁 (`/`) 支援手機瀏覽器即時查看感測數值。

---

## 系統架構

```
┌──────────────────────────────────────────────────────────────────────┐
│                        風機現場 (Edge)                                │
│                                                                      │
│  ┌───────────────────────────┐    ┌────────────────────────────────┐ │
│  │  AmebaPro2 (RTL8735B)     │    │  Raspberry Pi Pico W           │ │
│  │                           │    │                                │ │
│  │  Camera (FHD 30fps)       │    │  HC-SR04 超聲波感測器          │ │
│  │    │                      │    │  (TRIG: GP12 / ECHO: GP14)     │ │
│  │    ├─► RTSP Stream        │    │    │                           │ │
│  │    │    (port 554)        │    │    ├─► 距離 < 30cm             │ │
│  │    │                      │    │    │     └─► 紅燈 (GP16)       │ │
│  │    └─► NN Channel (RGB)   │    │    └─► 距離 ≥ 30cm             │ │
│  │         │                 │    │          └─► 綠燈 (GP17)       │ │
│  │         ▼                 │    │                                │ │
│  │  YOLOv4-Tiny 推論         │    │  HTTP Server (:80)             │ │
│  │    │                      │    │  └─► GET /api/data → JSON      │ │
│  │    ▼                      │    └────────────────────────────────┘ │
│  │  軌跡追蹤 (3 birds × 8f)  │                                       │
│  │    │                      │                                       │
│  │    ▼                      │                                       │
│  │  碰撞風險判斷              │                                       │
│  │    ├─► 安全 → 靜音         │                                       │
│  │    └─► 危險 → 蜂鳴器 (P24)│                                       │
│  │         + OSD DANGER 文字 │                                       │
│  └───────────────────────────┘                                       │
└──────────────────────────────────────────────────────────────────────┘
                  │ RTSP (H.264)            │ HTTP JSON
                  ▼                         ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    監控端 (遠端 / 本地瀏覽器)                         │
│                                                                     │
│  rtsp-to-websocket-server.js                                        │
│  └─► FFmpeg (RTSP → MJPEG) → WebSocket (:8082)                     │
│          │                                                          │
│          ▼                                                          │
│  face_recognition_dashboard.html / dashboard.html                  │
│  └─► 即時視訊 + 感測數據整合顯示                                    │
└─────────────────────────────────────────────────────────────────────┘
```

### 資料流：鳥類危險軌跡偵測

```
Camera FHD 串流
    │
    ├─► RTSP Channel 0 ──────────────────────► 遠端監控
    │
    └─► NN Channel 3 (576×320 RGB)
            │
            ▼
        YOLOv4-Tiny 推論 (10fps)
            │
            ▼
        ODPostProcess callback
            │
            ├─ 過濾非目標類別 (ObjectClassList.h)
            │
            ├─ 匹配至現有追蹤器 (歐式距離 < 150px)
            │    ├─ 命中 → 更新環形歷史緩衝
            │    └─ 未命中 → missedFrames++，超過 5 幀則釋放
            │
            ├─ 新偵測 → 開啟新追蹤器 (最多 3 個)
            │
            ├─ 計算向量：鳥的位置 → TURBINE ZONE 中心
            │
            ├─ 判斷箭頭終點是否落入危險區
            │    ├─ 是 → 蜂鳴器 PWM + OSD "!!! DANGER !!!"
            │    └─ 否 → 靜音
            │
            └─ OSD 更新：軌跡線、預測箭頭、危險區框
```

---

## 技術棧

### 邊緣 AI 模組（AmebaPro2 / RTL8735B）

| 元件 | 說明 |
|------|------|
| `VideoStream` | 雙通道視訊擷取（FHD 顯示 + RGB 推論） |
| `NNObjectDetection` | YOLOv4-Tiny 物件偵測框架 |
| `RTSP` | H.264 即時串流伺服器 |
| `VideoStreamOverlay (OSD)` | 畫面疊加（邊界框、文字、線條） |
| `StreamIO` | 多路視訊資料流路由 |

**自訂演算法**

| 模組 | 說明 |
|------|------|
| `TrackedBird` 結構體 | 環形緩衝歷史位置（8 幀）+ 失蹤幀數計數 |
| `distSq()` | 整數平方距離，避免浮點開根號 |
| `inDangerZone()` | 比例座標轉像素座標後的矩形命中判斷 |
| `drawArrow()` | OSD 帶箭頭方向線（主線 + 35° 雙翼） |

### 超聲波感測模組（Raspberry Pi Pico W）

| 元件 | 說明 |
|------|------|
| `WebServer` | Arduino 內建 HTTP 伺服器（port 80） |
| `pulseIn()` | HC-SR04 Echo 計時（30ms 超時） |
| CORS 標頭 | 允許 Dashboard 跨域存取 `/api/data` |

### 串流橋接（Node.js）

| 套件 | 版本 | 用途 |
|------|------|------|
| `ws` | — | WebSocket 伺服器（port 8082） |
| `fluent-ffmpeg` | — | FFmpeg 封裝，RTSP → MJPEG 轉碼 |
| `ffmpeg-static` | — | 內建 FFmpeg 二進位（備用） |

---

## 硬體清單

| 元件 | 型號 | 數量 | 用途 |
|------|------|------|------|
| 邊緣 AI 視覺模組 | AmebaPro2 HUB-8735_ultra (RTL8735B) | 1 | 鳥類偵測 + RTSP 串流 |
| 微控制器 | Raspberry Pi Pico W | 1 | 超聲波感測 + HTTP API |
| 超聲波感測器 | HC-SR04 | 1 | 近端距離量測 |
| 蜂鳴器 | 主動式蜂鳴器 | 1 | 聲波驅離（接 AmebaPro2 P24） |
| 指示 LED | 紅色 LED | 1 | 近端危險指示（GP16） |
| 指示 LED | 綠色 LED | 1 | 安全狀態指示（GP17） |

### 腳位對應

**AmebaPro2 (RTL8735B)**

| 腳位 | 功能 |
|------|------|
| P24 | 蜂鳴器（HIGH = 關，LOW / PWM = 啟動） |

**Raspberry Pi Pico W**

| 腳位 | 元件 | 說明 |
|------|------|------|
| GP12 | HC-SR04 TRIG | 觸發超聲波 |
| GP14 | HC-SR04 ECHO | 接收回波（建議分壓至 3.3V） |
| GP16 | 紅色 LED | 距離 < 30cm 時亮起 |
| GP17 | 綠色 LED | 距離 ≥ 30cm 時亮起 |

---

## 專案結構

```
HLB-Smart-Bird-Repellent-System/
├── ObjectDetectionCallback-final/
│   ├── ObjectDetectionCallback-final.ino  # AmebaPro2 主程式：鳥類偵測 + 軌跡追蹤
│   └── ObjectClassList.h                  # 偵測目標清單（pigeon、dunlin）
├── distance/
│   └── distance.ino                       # Pico W 主程式：超聲波感測 + HTTP API
├── 8735u/
│   └── 8735u.ino                          # AmebaPro2 人臉辨識模組（輔助用）
├── rtsp-to-websocket-server.js            # RTSP → WebSocket 橋接服務
├── face_recognition_dashboard.html        # 人臉辨識即時 Dashboard
├── dashboard.html                         # 主控 IoT Dashboard
├── package.json                           # Node.js 依賴
└── README.md
```

---

## 安裝與部署

### 前置需求

- Arduino IDE 2.x，已安裝 **AmebaPro2 (RTL8735B)** 開發板套件
- Arduino IDE，已安裝 **Raspberry Pi Pico / RP2040** 開發板套件
- Node.js ≥ 16（用於串流橋接服務）

---

### 1. 部署 AmebaPro2 鳥類偵測程式

```bash
# 在 Arduino IDE 開啟
ObjectDetectionCallback-final/ObjectDetectionCallback-final.ino
```

修改 WiFi 設定（檔案第 56-57 行）：

```cpp
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";
```

選擇開發板：`Tools > Board > AmebaPro2 Series > RTL8735B`

上傳後開啟 Serial Monitor（115200 baud），取得 RTSP 串流網址：

```
Network URL for RTSP Streaming: rtsp://192.168.x.x:554
```

---

### 2. 部署 Raspberry Pi Pico W 超聲波模組

```bash
# 在 Arduino IDE 開啟
distance/distance.ino
```

修改 WiFi 設定（檔案第 5-6 行）：

```cpp
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

選擇開發板：`Tools > Board > Raspberry Pi Pico / RP2040 > Raspberry Pi Pico W`

上傳後開啟 Serial Monitor，取得 HTTP API 網址：

```
✅ WiFi 連線成功！
IP 地址: 192.168.x.x
API: http://192.168.x.x/api/data
```

---

### 3. 啟動 RTSP→WebSocket 橋接服務

```bash
# 安裝依賴
npm install

# 啟動服務（預設監聽 ws://localhost:8082）
npm start
```

服務啟動後，`face_recognition_dashboard.html` 輸入 AmebaPro2 的 IP，即可在瀏覽器中觀看即時串流。

詳細設定請參閱 [README_RTSP轉換器.md](./README_RTSP轉換器.md)。

---

### 4. 開啟 Dashboard

直接用瀏覽器開啟 `dashboard.html`，輸入各設備 IP 即可查看整合監控介面。

---

## API 端點說明

### Raspberry Pi Pico W — 超聲波感測

Base URL：`http://<PICO_W_IP>`

| 端點 | 方法 | 功能 |
|------|------|------|
| `/` | GET | 網頁版即時距離顯示（每秒自動刷新） |
| `/api/data` | GET | JSON 格式距離數據 |

#### `/api/data` 回應範例

```json
{
  "distance": 24.50,
  "distance_cm": 24.50,
  "distance_m": 0.245,
  "valid": true,
  "timestamp": 123456,
  "red_led": true,
  "green_led": false
}
```

| 欄位 | 說明 |
|------|------|
| `distance` / `distance_cm` | 量測距離（公分） |
| `distance_m` | 量測距離（公尺） |
| `valid` | `true` = 正常回波；`false` = 無訊號（超出量程） |
| `red_led` | `true` = 距離 < 30cm，紅燈亮 |
| `green_led` | `true` = 距離 ≥ 30cm，綠燈亮 |

---

## 環境變數設定

本專案不使用雲端 API 金鑰，所有推論均在邊緣端完成。唯一需要設定的參數為**各裝置的 WiFi SSID 與密碼**，直接寫入對應的 Arduino `.ino` 檔案中（詳見上方安裝步驟）。

> **注意**：請勿將含有真實 WiFi 密碼的 `.ino` 檔案提交至公開版本控制。建議提交前替換為佔位字元，或透過 `#define` 搭配本地未追蹤的設定標頭檔管理憑證。

---

## 目標鳥種

| 索引 | 鳥種（英文） | 鳥種（中文） |
|------|------------|------------|
| 0 | pigeon | 鴿子 |
| 1 | dunlin | 黑腹濱鷸 |

如需新增或修改偵測鳥種，編輯 `ObjectDetectionCallback-final/ObjectClassList.h`，並重新訓練或替換 YOLO 模型。

---

## 授權

本專案為 **2026 智慧創新大賞 (Best AI Awards)** 參賽作品。
