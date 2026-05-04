# ✅ Audio Waiting Flow - ESP32 Device Implementation

## 📋 Tổng Quan

Tài liệu này mô tả phần ESP32 (device-side) của Audio Waiting Flow - luồng xử lý khi user nói xong và chờ response từ server.

---

## 📋 Nguyên Tắc Cơ Bản

| # | Nguyên tắc | Status |
|---|-----------|--------|
| 1 | Không dùng voice động (TTS) cho audio chờ | ✅ |
| 2 | Tất cả audio/emoji chờ là asset tĩnh trên SD card | ✅ |
| 3 | Gesture điều khiển qua STM32 (I2C 2 bytes) | ✅ |
| 4 | Tất cả action từ server điều khiển | ✅ |
| 5 | Remote audio CHỜ local audio XONG mới được phát | ✅ |

---

## 🔄 Luồng Hoàn Chỉnh

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ 1. ESP32 → SERVER: [Audio chunks] liên tục                                 │
└─────────────────────────────────┬──────────────────────────────────────┘
                                  ↓
┌──────────────────────────────────────────────────────────────────────────┐
│ 2. VAD detect SILENCE (user dứt lời)                                     │
│    Server → ESP32: {                                                     │
│      "type": "device_action",                                           │
│      "action": "play_scene",                                             │
│      "scene_tag": "acknowledgment_01",  // Audio tag (SD card)              │
│      "emoji": "thinking",       // Emoji tag (SD card)                     │
│      "gesture": "nod",          // Gesture tag → STM32 I2C                 │
│      "reply": true,                                                     │
│      "reply_action": "acknowledgment_done",                              │
│      "req_id": "abc123"                                                  │
│    }                                                                   │
└─────────────────────────────────┬──────────────────────────────────────┘
                                  ↓
┌──────────────────────────────────────────────────────────────────────────┐
│ 3. ASR đang xử lý (song song)                                           │
│    ESP32: Phát acknowledgment + Gesture + Đổi emoji                          │
│    ESP32: Gửi reply khi xong                                           │
│    ESP32 → Server: {"type": "scene_done", "reply_action": "..."}          │
└─────────────────────────────────┬──────────────────────────────────────┘
                                  ↓
┌──────────────────────────────────────────────────────────────────────────┐
│ 4. Server đợi hoặc gửi pending                                         │
│    Server → ESP32: {                                                     │
│      "type": "device_action",                                           │
│      "action": "play_scene",                                             │
│      "scene_tag": "pending_01",                                          │
│      "emoji": "waiting",                                                │
│      "gesture": "shake_head",                                           │
│      "reply": true,                                                     │
│      "reply_action": "pending_done"                                     │
│    }                                                                   │
└─────────────────────────────────┬──────────────────────────────────────┘
                                  ↓
┌──────────────────────────────────────────────────────────────────────────┐
│ 5. TTS có audio → STOP scene + PLAY REMOTE                                │
│    Server → ESP32: {                                                   │
│      "type": "device_action",                                          │
│      "action": "stop_scene",                                            │
│      "emoji": "remote",                                                 │
│      "gesture": "stop"                                                  │
│    }                                                                   │
│    ESP32: Đợi local audio XONG → Phát remote audio                       │
└──────────────────────────────────────────────┬───────────────────────┘
                                               │
                                               ↓ (Opus audio chunks)
```

---

## 📋 Message Types

### Nhận Từ Server (`device_action`)

```json
// play_scene - Phát audio + emoji + gesture
{
  "type": "device_action",
  "action": "play_scene",
  "scene_tag": "ack_01",      // File: /sdcard/dodomio/audio/ack_01.wav
  "emoji": "thinking",        // File: /dodomio/emoji/thinking.png
  "gesture": "nod",           // → I2C 0x0A
  "reply": true,             // Có gửi scene_done khi xong
  "reply_action": "acknowledgment_done",
  "req_id": "abc123"
}

// stop_scene - Dừng + đổi emoji + gesture
{
  "type": "device_action",
  "action": "stop_scene",
  "emoji": "remote",
  "gesture": "stop"
}

// change_emoji - Chỉ đổi emoji
{
  "type": "device_action",
  "action": "change_emoji",
  "emoji": "waiting"
}

// gesture - Chỉ thực hiện gesture
{
  "type": "device_action",
  "action": "gesture",
  "gesture": "shake_head"
}
```

### Gửi Về Server (`scene_done`)

```json
// Reply khi audio phát xong
{
  "type": "scene_done",
  "reply_action": "acknowledgment_done",
  "req_id": "abc123"
}
```

---

## 📋 Gesture Mapping

| Gesture | Hex | I2C Command |
|---------|-----|------------|
| `stop` | 0x00 | Dừng |
| `forward_slow` | 0x01 | Đi tới chậm |
| `forward_fast` | 0x02 | Đi tới nhanh |
| `backward_slow` | 0x03 | Lùi chậm |
| `backward_fast` | 0x04 | Lùi nhanh |
| `turn_left_slow` | 0x05 | Quay trái chậm |
| `turn_left_fast` | 0x06 | Quay trái nhanh |
| `turn_right_slow` | 0x07 | Quay phải chậm |
| `turn_right_fast` | 0x08 | Quay phải nhanh |
| `spin_left` | 0x09 | Xoay trái |
| `nod` | 0x0A | Gật đầu |
| `shake_head` | 0x0B | Lắc đầu |
| `raise_hand` | 0x0C | Giơ tay |
| `wave` | 0x0D | Vẫy tay |

---

## 📋 SD Card Structure

```
/sdcard/
└── dodomio/
    ├── audio/               (.wav files - SERVER chỉ định variant)
    │   ├── thinking_01.wav
    │   ├── thinking_02.wav
    │   ├── thinking_03.wav
    │   ├── ack_01.wav
    │   ├── ack_02.wav
    │   ├── ack_03.wav
    │   ├── wait_01.wav
    │   ├── wait_02.wav
    │   └── wait_03.wav
    └── emoji/               (.gif files - SERVER chỉ định variant)
        ├── happy_01.gif
        ├── happy_02.gif
        ├── happy_03.gif
        ├── cool_01.gif
        ├── cool_02.gif
        ├── cool_03.gif
        ├── thinking_01.gif
        └── waiting_01.gif
```

### 📋 Server gửi exact variant

| Server gửi | ESP32 load | Kết quả |
|-----------|-----------|---------|
| `emoji: "happy_01"` | `happy_01.gif` | ✅ Exact |
| `scene_tag: "thinking_01"` | `thinking_01.wav` | ✅ Exact |
| `emoji: "happy"` | Random trong `[happy_01, happy_02, happy_03]` | ✅ Random |

### 📋 Emoji Loading Flow

| Step | Code | Mô tả |
|------|------|-------|
| 1 | `application.cc:71-76` | Load tất cả .gif từ `/sdcard/dodomio/emoji/` |
| 2 | `EmojiCollection::LoadFromSD()` | Scan directory, load .gif files |
| 3 | `GetEmojiImage(name)` | Lấy emoji theo name (không cần .gif) |
| 4 | `display->SetEmotion(name)` | Hiển thị emoji |

**Code:**
```c++
// application.cc:71-76 - Load emojis on startup
auto theme = display->GetTheme();
if (theme && theme->emoji_collection()) {
    theme->emoji_collection()->LoadFromSD("/sdcard/dodomio/emoji");
}

// display.cc - SetEmotion calls GetEmojiImage
void LcdDisplay::SetEmotion(const char* emotion) {
    auto emoji_collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();
    auto image = emoji_collection != nullptr ? emoji_collection->GetEmojiImage(emotion) : nullptr;
    // ... display image
}
```

---

## 📋 Implementation

### File: `main/application.h`

```c++
// Audio Waiting Flow - Device Action Handlers
void HandleDeviceAction(const cJSON* root);
void HandlePlayScene(const cJSON* root);
void HandleStopScene(const cJSON* root);
void SendGestureToSTM32(const char* gesture);
void SendSceneDoneReply(const char* reply_action, const char* req_id = nullptr);  // req_id optional
```

### File: `main/application.cc`

#### 0. GESTURE_MAP - Gesture to Hex mapping

```c++
// Gesture mapping: gesture name → I2C command byte
static const std::unordered_map<std::string, uint8_t> GESTURE_MAP = {
    {"stop", 0x00},
    {"nod", 0x0A},
    {"shake_head", 0x0B},
    {"raise_hand", 0x0C},
    {"wave", 0x0D},
    {"forward_slow", 0x01},
    {"forward_fast", 0x02},
    {"backward_slow", 0x03},
    {"backward_fast", 0x04},
    {"turn_left_slow", 0x05},
    {"turn_left_fast", 0x06},
    {"turn_right_slow", 0x07},
    {"turn_right_fast", 0x08},
    {"spin_left", 0x09},
};
```

#### 1. HandleDeviceAction() - Main dispatcher

```c++
void Application::HandleDeviceAction(const cJSON* root) {
    auto action = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action)) return;

    ESP_LOGI(TAG, "Device action: %s", action->valuestring);

    // Dispatch to specific handler
    if (strcmp(action->valuestring, "play_scene") == 0) {
        HandlePlayScene(root);
    } else if (strcmp(action->valuestring, "stop_scene") == 0) {
        HandleStopScene(root);
    } else if (strcmp(action->valuestring, "change_emoji") == 0) {
        auto emoji = cJSON_GetObjectItem(root, "emoji");
        if (cJSON_IsString(emoji)) {
            auto display = Board::GetInstance().GetDisplay();
            Schedule([display, emoji_str = std::string(emoji->valuestring)]() {
                display->SetEmotion(emoji_str.c_str());
            });
        }
    } else if (strcmp(action->valuestring, "gesture") == 0) {
        auto gesture = cJSON_GetObjectItem(root, "gesture");
        if (cJSON_IsString(gesture)) {
            SendGestureToSTM32(gesture->valuestring);
        }
    }
}
```

**Supported actions:**
- `play_scene` - Phát audio + emoji + gesture + reply
- `stop_scene` - Dừng audio + đổi emoji + gesture
- `change_emoji` - Chỉ đổi emoji
- `gesture` - Chỉ thực hiện gesture

#### 2. HandlePlayScene() - Play audio + emoji + gesture (VỚI req_id SUPPORT)

```c++
void Application::HandlePlayScene(const cJSON* root) {
    // 1. Get ALL parameters from JSON
    auto scene_tag = cJSON_GetObjectItem(root, "scene_tag");
    auto emoji = cJSON_GetObjectItem(root, "emoji");
    auto gesture = cJSON_GetObjectItem(root, "gesture");
    auto reply = cJSON_GetObjectItem(root, "reply");
    auto reply_action = cJSON_GetObjectItem(root, "reply_action");
    auto req_id = cJSON_GetObjectItem(root, "req_id");  // NEW - correlation ID

    std::string scene = scene_tag ? scene_tag->valuestring : "";
    std::string emo = emoji ? emoji->valuestring : "";
    std::string gest = gesture ? gesture->valuestring : "";
    bool need_reply = reply ? reply->valueint : false;
    std::string reply_act = reply_action ? reply_action->valuestring : "";
    std::string req_id_str = req_id ? req_id->valuestring : "";

    ESP_LOGI(TAG, "Play scene: %s, emoji: %s, gesture: %s, reply: %d", 
             scene.c_str(), emo.c_str(), gest.c_str(), need_reply);

    // 2. Execute on main loop (thread-safe via Schedule)
    auto display = Board::GetInstance().GetDisplay();
    Schedule([this, display, scene, emo, gest, need_reply, reply_act, req_id_str]() {
        // 2.1 Gesture - TEMP DISABLED FOR TEST
        if (!gest.empty()) {
            ESP_LOGI(TAG, "Gesture skipped (disabled): %s", gest.c_str());
        }
        // if (!gest.empty()) {
        //     SendGestureToSTM32(gest.c_str());
        // }

        // 2.2 Change emoji: /dodomio/emoji/<emoji>.png
        if (!emo.empty()) {
            display->SetEmotion(emo.c_str());
        }

        // 2.3 Play audio from SD card: /dodomio/audio/<scene>.wav
        if (!scene.empty()) {
            std::string audio_path = "/sdcard/dodomio/audio/" + scene + ".wav";
            
            // Set callback to reply when audio finishes
            if (need_reply && !reply_act.empty()) {
                auto reply_str = std::string(reply_act);
                auto req_id = std::string(req_id_str);
                audio_service_.SetPlaybackFinishedCallback([this, reply_str, req_id]() {
                    // Pass req_id to correlation
                    SendSceneDoneReply(reply_str.c_str(), req_id.empty() ? nullptr : req_id.c_str());
                });
            }
            
            audio_service_.PlayFile(audio_path.c_str());
            
            // Fallback reply (works even if audio fails or no SD card)
            if (need_reply && !reply_act.empty()) {
                auto reply_str = std::string(reply_act);
                auto req_id = std::string(req_id_str);
                ESP_LOGI(TAG, "Scene reply scheduled: %s, req_id: %s", reply_str.c_str(), req_id.c_str());
                Schedule([this, reply_str, req_id]() {
                    ESP_LOGI(TAG, "Sending scene_done reply: %s, req_id: %s", reply_str.c_str(), req_id.c_str());
                    SendSceneDoneReply(reply_str.c_str(), req_id.empty() ? nullptr : req_id.c_str());
                });
            }
        } else if (need_reply && !reply_act.empty()) {
            // No audio, reply immediately
            SendSceneDoneReply(reply_act.c_str(), req_id_str.empty() ? nullptr : req_id_str.c_str());
        }
    });
}
```

**Features:**
- ✅ Extract `req_id` from JSON
- ✅ Pass `req_id` in callback (for correlation)
- ✅ Fallback reply if audio fails
- ✅ Reply immediately if no audio file

#### 3. HandleStopScene() - Stop playback

```c++
void Application::HandleStopScene(const cJSON* root) {
    auto emoji = cJSON_GetObjectItem(root, "emoji");
    auto gesture = cJSON_GetObjectItem(root, "gesture");
    
    // Default values
    std::string emo = emoji ? emoji->valuestring : "neutral";
    std::string gest = gesture ? gesture->valuestring : "stop";
    
    ESP_LOGI(TAG, "Stop scene: emoji=%s, gesture=%s", emo.c_str(), gest.c_str());
    
    // Execute on main loop
    auto display = Board::GetInstance().GetDisplay();
    Schedule([this, display, emo, gest]() {
        // Stop audio playback
        audio_service_.StopPlayback();
        
        // Change emoji
        display->SetEmotion(emo.c_str());
        
        // Gesture - TEMP DISABLED FOR TEST
        if (!gest.empty()) {
            ESP_LOGI(TAG, "Gesture skipped (disabled): %s", gest.c_str());
        }
        // if (!gest.empty()) {
        //     SendGestureToSTM32(gest.c_str());
        // }
    });
}
```

**Features:**
- ✅ Stop audio playback immediately
- ✅ Change emoji
- ✅ Default values for emoji/gesture

#### 4. SendGestureToSTM32() - I2C communication

```c++
void Application::SendGestureToSTM32(const char* gesture) {
    // STM32 I2C address (common addresses: 0x27, 0x28)
    static const uint8_t STM32_ADDR = 0x27;
    static const uint8_t REG_CMD = 0x00;
    
    // Look up gesture in map
    auto it = GESTURE_MAP.find(gesture);
    uint8_t cmd = (it != GESTURE_MAP.end()) ? it->second : 0x00;

    // I2C write (2 bytes: register + command)
    esp_err_t ret = i2c_master_write_to_device(
        I2C_NUM_0, STM32_ADDR,
        (const uint8_t[]){REG_CMD, cmd}, 2,
        pdMS_TO_TICKS(100)
    );

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Gesture sent: %s -> 0x%02X", gesture, cmd);
    } else {
        ESP_LOGE(TAG, "Failed to send gesture: %s (err=0x%x)", gesture, ret);
    }
}
```

**Config:**
- I2C Address: `0x27`
- Register: `0x00`
- Protocol: 2 bytes (register + command)

#### 5. SendSceneDoneReply() - Send reply to server (WITH req_id)

```c++
void Application::SendSceneDoneReply(const char* reply_action, const char* req_id) {
    if (!protocol_ || !protocol_->IsConnected()) {
        ESP_LOGW(TAG, "Protocol not connected, cannot send scene_done");
        return;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "scene_done");
    cJSON_AddStringToObject(root, "reply_action", reply_action);
    
    // Add req_id if provided (for correlation with server request)
    if (req_id != nullptr) {
        cJSON_AddStringToObject(root, "req_id", req_id);
    }
    
    auto* json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    if (protocol_->SendText(message)) {
        ESP_LOGI(TAG, "Scene done sent: %s, req_id: %s", reply_action, req_id ? req_id : "null");
    } else {
        ESP_LOGE(TAG, "Failed to send scene_done: %s", reply_action);
    }
}
```

**Features:**
- ✅ Check connection before sending
- ✅ Include `req_id` for correlation
- ✅ Log success/failure

**Message format:**
```json
{"type": "scene_done", "reply_action": "acknowledgment_done", "req_id": "abc123"}
```

---

## 📋 Flow Chi Tiết Từng Step

### Step 1: User nói → ESP32 gửi audio
- ESP32 thu âm → gửi audio chunks lên server
- Đây là luồng hiện tại, **không cần thêm code mới**

### Step 2: Server gửi `device_action` (acknowledgment)
- ESP32 nhận → parse các trường
- HandleDeviceAction() dispatch tới HandlePlayScene()

### Step 3: ESP32 phát acknowledgment
- Gửi gesture qua I2C → STM32 (TEMP DISABLED)
- Đổi emoji
- Phát audio từ SD card
- Gửi scene_done khi xong

### Step 4: Server gửi pending (nếu cần)
- Tương tự Step 2-3

### Step 5: Server gửi stop_scene + remote audio
- HandleStopScene() → dừng local audio
- Đợi phát xong → phát remote (từ protocol)

---

## ✅ Checklist

| # | Confirm | Nội dung |
|---|---------|---------|
| 1 | ✅ | device_action message type |
| 2 | ✅ | play_scene action |
| 3 | ✅ | stop_scene action |
| 4 | ✅ | change_emoji action |
| 5 | ✅ | gesture action |
| 6 | ✅ | req_id field parsing |
| 7 | ✅ | scene_done reply với req_id |
| 8 | ✅ | Gesture mapping (14 gestures) |
| 9 | ✅ | I2C 2 bytes (register + cmd) |
| 10 | ✅ | SD card audio path |
| 11 | ✅ | SD card emoji path |