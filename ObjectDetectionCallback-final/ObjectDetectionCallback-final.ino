#include "WiFi.h"
#include "StreamIO.h"
#include "VideoStream.h"
#include "RTSP.h"
#include "NNObjectDetection.h"
#include "VideoStreamOverlay.h"
#include "ObjectClassList.h"

#define CHANNEL     0
#define CHANNELNN   3

#define NNWIDTH  576
#define NNHEIGHT 320

// ===== 軌跡追蹤參數 =====
#define MAX_BIRDS      3    // 最多同時追蹤幾隻鳥
#define HISTORY_LEN    8    // 保留幾個歷史位置
#define PREDICT_FRAMES 20   // 預測幾幀後的位置
#define MAX_MISSED     5    // 消失幾幀後放棄追蹤
#define MATCH_DIST     150  // 新偵測與舊追蹤點的最大匹配距離(像素)

// ===== 危險區範圍 (佔畫面比例) =====
#define DANGER_X1  0.30f
#define DANGER_Y1  0.15f
#define DANGER_X2  0.70f
#define DANGER_Y2  0.85f

struct TrackedBird {
    bool  active;
    int   cx[HISTORY_LEN];
    int   cy[HISTORY_LEN];
    int   histCount;
    int   missedFrames;
};

TrackedBird birds[MAX_BIRDS];

VideoSetting config(VIDEO_FHD, 30, VIDEO_H264, 0);
VideoSetting configNN(NNWIDTH, NNHEIGHT, 10, VIDEO_RGB, 0);
NNObjectDetection ObjDet;
RTSP rtsp;
StreamIO videoStreamer(1, 1);
StreamIO videoStreamerNN(1, 1);

const int BUZZER_PIN = 24;

char ssid[] = "Xin";
char pass[] = "0720samson";
int status = WL_IDLE_STATUS;

IPAddress ip;
int rtsp_portnum;

void ODPostProcess(std::vector<ObjectDetectionResult> results);

// ===== OSD 顏色 =====
#define COLOR_TRAIL_DIM    OSD_COLOR_WHITE   // 舊軌跡（白，細）
#define COLOR_TRAIL_MID    OSD_COLOR_YELLOW  // 中段軌跡（黃）
#define COLOR_TRAIL_BRIGHT OSD_COLOR_YELLOW  // 最近軌跡（黃）
#define COLOR_ARROW        OSD_COLOR_YELLOW  // 預測箭頭（黃）
#define COLOR_ARROWHEAD    OSD_COLOR_RED     // 箭頭尖端（紅）
#define COLOR_DANGER_ZONE  OSD_COLOR_CYAN    // 危險框（青）
#define COLOR_DANGER_ALERT OSD_COLOR_RED     // DANGER 文字（紅）

// ===== 工具函式 =====
int distSq(int x1, int y1, int x2, int y2) {
    return (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
}

bool inDangerZone(int x, int y, int im_w, int im_h) {
    int dx1 = (int)(DANGER_X1 * im_w);
    int dy1 = (int)(DANGER_Y1 * im_h);
    int dx2 = (int)(DANGER_X2 * im_w);
    int dy2 = (int)(DANGER_Y2 * im_h);
    return (x >= dx1 && x <= dx2 && y >= dy1 && y <= dy2);
}

// 畫帶箭頭的預測線
void drawArrow(int x1, int y1, int x2, int y2) {
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 5.0f) return;

    // 主線（兩段，統一粗線）
    int mx = (x1 + x2) / 2;
    int my = (y1 + y2) / 2;
    OSD.drawLine(CHANNEL, x1, y1, mx, my, 10, COLOR_ARROW);
    OSD.drawLine(CHANNEL, mx, my, x2, y2, 10, COLOR_ARROWHEAD);

    // 計算反向單位向量（箭頭翼從尖端向後延伸）
    float nx = -dx / len;
    float ny = -dy / len;
    int   headLen = 18;

    // 旋轉 ±35 度畫兩條翼線
    float cos35 = 0.819f;
    float sin35 = 0.574f;

    int w1x = x2 + (int)((nx * cos35 - ny * sin35) * headLen);
    int w1y = y2 + (int)((nx * sin35 + ny * cos35) * headLen);
    int w2x = x2 + (int)((nx * cos35 + ny * sin35) * headLen);
    int w2y = y2 + (int)((-nx * sin35 + ny * cos35) * headLen);

    OSD.drawLine(CHANNEL, x2, y2, w1x, w1y, 10, COLOR_ARROWHEAD);
    OSD.drawLine(CHANNEL, x2, y2, w2x, w2y, 10, COLOR_ARROWHEAD);
}

void setup()
{
    memset(birds, 0, sizeof(birds));

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, HIGH);

    Serial.begin(115200);

    while (status != WL_CONNECTED) {
        Serial.print("Attempting to connect to WPA SSID: ");
        Serial.println(ssid);
        status = WiFi.begin(ssid, pass);
        delay(2000);
    }

    ip = WiFi.localIP();

    config.setBitrate(2 * 1024 * 1024);
    Camera.configVideoChannel(CHANNEL, config);
    Camera.configVideoChannel(CHANNELNN, configNN);
    Camera.videoInit();

    rtsp.configVideo(config);
    rtsp.begin();
    rtsp_portnum = rtsp.getPort();

    ObjDet.configVideo(configNN);
    ObjDet.modelSelect(OBJECT_DETECTION, DEFAULT_YOLOV4TINY, NA_MODEL, NA_MODEL);
    ObjDet.begin();
    ObjDet.setResultCallback(ODPostProcess);

    videoStreamer.registerInput(Camera.getStream(CHANNEL));
    videoStreamer.registerOutput(rtsp);
    if (videoStreamer.begin() != 0) {
        Serial.println("StreamIO link start failed");
    }
    Camera.channelBegin(CHANNEL);

    videoStreamerNN.registerInput(Camera.getStream(CHANNELNN));
    videoStreamerNN.setStackSize();
    videoStreamerNN.setTaskPriority();
    videoStreamerNN.registerOutput(ObjDet);
    if (videoStreamerNN.begin() != 0) {
        Serial.println("StreamIO link start failed");
    }
    Camera.channelBegin(CHANNELNN);

    OSD.configVideo(CHANNEL, config);
    OSD.begin();
}

void loop()
{
    delay(2000);
    OSD.createBitmap(CHANNEL);
    OSD.update(CHANNEL);
}

void ODPostProcess(std::vector<ObjectDetectionResult> results)
{
    uint16_t im_h = config.height();
    uint16_t im_w = config.width();

    Serial.print("Network URL for RTSP Streaming: rtsp://");
    Serial.print(ip);
    Serial.print(":");
    Serial.println(rtsp_portnum);

    OSD.createBitmap(CHANNEL);

    // ===== 畫危險區半透明框 =====
    int dx1 = (int)(DANGER_X1 * im_w);
    int dy1 = (int)(DANGER_Y1 * im_h);
    int dx2 = (int)(DANGER_X2 * im_w);
    int dy2 = (int)(DANGER_Y2 * im_h);
    OSD.drawRect(CHANNEL, dx1, dy1, dx2, dy2, 2, COLOR_DANGER_ZONE);
    OSD.drawText(CHANNEL, dx1, dy1 - OSD.getTextHeight(CHANNEL), "TURBINE ZONE", COLOR_DANGER_ZONE);

    // ===== 收集本幀偵測到的鳥中心點 =====
    int objCount = ObjDet.getResultCount();
    int detCx[10], detCy[10];
    int detCount = 0;

    for (int i = 0; i < objCount && detCount < 10; i++) {
        ObjectDetectionResult item = results[i];
        int idx = item.type();
        if (idx < (int)(sizeof(itemList) / sizeof(itemList[0])) && itemList[idx].filter == 1) {
            int xmin = (int)(item.xMin() * im_w);
            int xmax = (int)(item.xMax() * im_w);
            int ymin = (int)(item.yMin() * im_h);
            int ymax = (int)(item.yMax() * im_h);
            detCx[detCount] = (xmin + xmax) / 2;
            detCy[detCount] = (ymin + ymax) / 2;

            // 畫偵測框
            OSD.drawRect(CHANNEL, xmin, ymin, xmax, ymax, 3, OSD_COLOR_GREEN);
            char label[40];
            snprintf(label, sizeof(label), "%s", itemList[idx].objectName);
            OSD.drawText(CHANNEL, xmin, ymin - OSD.getTextHeight(CHANNEL), label, OSD_COLOR_GREEN);

            detCount++;
        }
    }

    // ===== 匹配偵測結果到現有追蹤器 =====
    bool matched[10] = {false};

    for (int b = 0; b < MAX_BIRDS; b++) {
        if (!birds[b].active) continue;

        int lastCx = birds[b].cx[birds[b].histCount - 1];
        int lastCy = birds[b].cy[birds[b].histCount - 1];
        int bestDist = MATCH_DIST * MATCH_DIST;
        int bestDet  = -1;

        for (int d = 0; d < detCount; d++) {
            if (matched[d]) continue;
            int dist = distSq(lastCx, lastCy, detCx[d], detCy[d]);
            if (dist < bestDist) {
                bestDist = dist;
                bestDet  = d;
            }
        }

        if (bestDet >= 0) {
            matched[bestDet] = true;
            birds[b].missedFrames = 0;
            // 更新歷史（環形緩衝）
            if (birds[b].histCount < HISTORY_LEN) {
                birds[b].cx[birds[b].histCount] = detCx[bestDet];
                birds[b].cy[birds[b].histCount] = detCy[bestDet];
                birds[b].histCount++;
            } else {
                for (int k = 0; k < HISTORY_LEN - 1; k++) {
                    birds[b].cx[k] = birds[b].cx[k + 1];
                    birds[b].cy[k] = birds[b].cy[k + 1];
                }
                birds[b].cx[HISTORY_LEN - 1] = detCx[bestDet];
                birds[b].cy[HISTORY_LEN - 1] = detCy[bestDet];
            }
        } else {
            birds[b].missedFrames++;
            if (birds[b].missedFrames > MAX_MISSED) {
                birds[b].active = false;
            }
        }
    }

    // ===== 未匹配的偵測開新追蹤器 =====
    for (int d = 0; d < detCount; d++) {
        if (matched[d]) continue;
        for (int b = 0; b < MAX_BIRDS; b++) {
            if (!birds[b].active) {
                birds[b].active       = true;
                birds[b].histCount    = 1;
                birds[b].missedFrames = 0;
                birds[b].cx[0]        = detCx[d];
                birds[b].cy[0]        = detCy[d];
                break;
            }
        }
    }

    // ===== 畫軌跡 + 預測 =====
    bool danger = false;

    for (int b = 0; b < MAX_BIRDS; b++) {
        if (!birds[b].active || birds[b].histCount < 2) continue;

        int n = birds[b].histCount;

        // 畫歷史軌跡（漸層黃線：舊→暗，新→亮）
        for (int k = 0; k < n - 1; k++) {
            uint32_t trailColor;
            float ratio = (float)k / (float)(HISTORY_LEN - 1);
            if (ratio < 0.4f)       trailColor = COLOR_TRAIL_DIM;
            else if (ratio < 0.75f) trailColor = COLOR_TRAIL_MID;
            else                    trailColor = COLOR_TRAIL_BRIGHT;

            OSD.drawLine(CHANNEL,
                         birds[b].cx[k],     birds[b].cy[k],
                         birds[b].cx[k + 1], birds[b].cy[k + 1],
                         2, trailColor);
        }

        // 固定箭頭：從鳥的位置指向危險區中心
        int targetX = (int)((DANGER_X1 + DANGER_X2) / 2.0f * im_w);
        int targetY = (int)((DANGER_Y1 + DANGER_Y2) / 2.0f * im_h);

        float dx = (float)(targetX - birds[b].cx[n - 1]);
        float dy = (float)(targetY - birds[b].cy[n - 1]);
        float len = sqrtf(dx * dx + dy * dy);
        int arrowLen = 80;  // 固定箭頭長度（像素）
        int predX = birds[b].cx[n - 1] + (len > 0 ? (int)(dx / len * arrowLen) : 0);
        int predY = birds[b].cy[n - 1] + (len > 0 ? (int)(dy / len * arrowLen) : arrowLen);

        // 畫箭頭
        drawArrow(birds[b].cx[n - 1], birds[b].cy[n - 1], predX, predY);

        // 判斷是否飛向危險區
        if (inDangerZone(predX, predY, im_w, im_h)) {
            danger = true;
        }
    }

    // ===== 危險警告 + 蜂鳴器 =====
    if (danger) {
        OSD.drawText(CHANNEL, im_w / 2 - 60, 10, "!!! DANGER !!!", COLOR_DANGER_ALERT);
        analogWrite(BUZZER_PIN, 127);
        Serial.println("WARNING: Bird heading to turbine zone!");
    } else {
        analogWrite(BUZZER_PIN, 0);
        digitalWrite(BUZZER_PIN, HIGH);
    }

    OSD.update(CHANNEL);
}
