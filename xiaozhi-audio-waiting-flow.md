# ✅ Audio Waiting Flow - Xiaozhi ESP32 Server

## 📋 Nguyên Tắc Cơ Bản

| # | Nguyên tắc | Trạng thái |
|---|-----------|-----------|
| 1 | Không dùng voice động (TTS) cho audio chờ | ✅ |
| 2 | Tất cả audio/emoji chờ là asset tĩnh trên SD card | ✅ |
| 3 | Gesture điều khiển qua STM32 (I2C 2 bytes) | ✅ |
| 4 | Tất cả action từ server điều khiển | ✅ |
| 5 | Remote audio CHỜ local audio XONG mới được phát | ✅ |
| 6 | Audio local 1 phát TRƯỚC ASR (khi VAD silence) | ✅ |

---

## 🔄 Luồng Hoàn Chỉnh

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ 1. ESP32 → SERVER: [Audio chunks] liên tục                │
└─────────────────────────────────┬───────────────────┘
                                  ↓
┌─────────────────────────────────────────────────────────────────┐
│ 2. VAD detect SILENCE (user dứt lời)                          │
│    → Configurable: silence_threshold_ms + acknowledgment_send_delay_ms      │
│    → Server gửi acknowledgment NGAY (trước ASR)                      │
│    Server → ESP32: {                                          │
│      "type": "device_action",                                  │
│      "action": "play_scene",                                   │
│      "scene_tag": "acknowledgment_01",      // Audio tag (SD card)          │
│      "emoji": "thinking",      // Emoji tag (SD card)         │
│      "gesture": "nod",         // Gesture tag → STM32 I2C       │
│      "reply": true,                                          │
│      "reply_action": "acknowledgment_done",                           │
│      "req_id": "abc123"                                     │
│    }                                                       │
└─────────────────────────────────┬───────────────────────────┘
                                  ↓
┌─────────────────────────────────────────────────────────────────┐
│ 3. ASR đang xử lý (song song)                                  │
│    ESP32: Phát acknowledgment + Gesture + Đổi emoji                │
│    ESP32: Gửi reply khi xong                                 │
│    ESP32 → Server: {"type": "scene_done", "reply_action": "acknowledgment_done"}│
└─────────────────────────────────┬───────────────────────────┘
                                  ↓
┌─────────────────────────────────────────────────────────────────┐
│ 4. ASR xong → startToChat() → LLM + TTS bắt đầu  │
│    Server: Nhận reply → check remote ready   │
│    ✓ Remote ready → Đợi (buffer) → PLAY REMOTE khi xong │
│    ✗ Chưa ready → Đợi 1s → Gửi change_emoji │
└─────────────────────────────────┬───────────────────────────┘
                                  ↓ (1s)
┌─────────────────────────────────────────────────────────────────┐
│ 5. Server → ESP32: Pending                                    │
│    {                                                         │
│      "type": "device_action",                                │
│      "action": "play_scene",                                 │
│      "scene_tag": "pending_01",                                 │
│      "emoji": "waiting",                                     │
│      "gesture": "shake_head",  // → STM32 I2C 0x0B            │
│      "reply": true,                                          │
│      "reply_action": "pending_done"                            │
│    }                                                         │
└─────────────────────────────────┬───────────────────────────┘
                                  ↓
┌─────────────────────────────────────────────────────────────────┐
│ 6. ESP32 phát pending + reply                                  │
│    Server: Nhận reply                                         │
│    → Nếu remote ready → Đợi xong → PLAY REMOTE                    │
│    → Nếu chưa → Visual waiting (chờ)                        │
└─────────────────────────────────┬───────────────────────────┘
                                  ↓
┌─────────────────────────────────────────────────────────────────┐
│ 7. TTS có audio → STOP scene TRƯỚC + Buffer                     │
│    Server → ESP32: {                                           │
│      "type": "device_action",                                  │
│      "action": "stop_scene",                                 │
│      "emoji": "remote",                                      │
│      "gesture": "stop"    // → STM32 I2C 0x00                 │
│    }                                                         │
│    ESP32: Đợi local audio XONG (KHÔNG ngắt đột ngột)             │
│    ESP32: reply("acknowledgment_done")                                │
│    Server: Gửi Opus audio chunks                              │
│    ESP32: PLAY REMOTE audio                                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📋 Giải Thích Chi Tiết Từng Step

| Step | Tên | Nhiệm Vụ | Code Mới? |
|------|-----|----------|----------|
| **1** | User nói → Server | Audio chunks từ ESP32 lên | ❌ Có sẵn |
| **2** | VAD silence → Gửi Acknowledgment | Gửi device_action cho ESP32 | ✅ Cần thêm |
| **3** | ESP32 phát Acknowledgment | Phát audio + gửi reply | ✅ ESP32 |
| **4** | ASR xong → Check | Kiểm tra remote ready | ✅ Cần thêm |
| **5** | Gửi Pending | Sau 1s, gửi pending | ✅ Cần thêm |
| **6** | ESP32 phát Pending | Phát audio + reply | ✅ ESP32 |
| **7** | TTS ready → Remote | STOP scene + phát audio | ✅ Cần thêm |

### Chi Tiết Từng Step:

**Step 1: ESP32 → SERVER: [Audio chunks] liên tục**
- User đang nói
- ESP32 thu âm → gửi audio chunks lên server
- Server nhận audio → buffer
- ⚠️ ĐÂY LÀ LUỒNG HIỆN TẠI - **KHÔNG CẦN THÊM CODE MỚI**

**Step 2: VAD detect SILENCE → Gửi Acknowledgment**
- VAD phát hiện user đã dừng nói (silence threshold)
- Server gửi device_action cho ESP32 (trước khi ASR xử lý)
- ✅ CẦN THÊM: send_device_action() trong receiveAudioHandle.py

**Step 3: ASR đang xử lý + ESP32 phát Acknowledgment**
- ASR đang xử lý audio (song song)
- ESP32 phát acknowledgment từ SD card
- ESP32 gửi gesture qua I2C → STM32
- ESP32 đổi emoji
- ESP32 gửi scene_done khi xong
- ✅ CẦN THÊM: ESP32 xử lý device_action

**Step 4: ASR xong → Check Remote Ready**
- ASR hoàn thành → startToChat() → LLM + TTS
- Kiểm tra remote_audio_ready flag
- ✓ Remote ready → Phát remote audio
- ✗ Chưa ready → Đợi 1s → Gửi Pending
- ✅ CẦN THÊM: sceneDoneHandler.py

**Step 5: Server → ESP32: Pending (sau 1s)**
- Delay configurable (mặc định 1000ms)
- Random chọn pending từ danh sách
- ✅ CẦN THÊM: _schedule_pending() trong sceneDoneHandler.py

**Step 6: ESP32 phát Pending + Reply**
- ESP32 phát pending từ SD card
- ESP32 gửi scene_done khi xong
- Server nhận reply → Check remote ready
- ✅ CẦN THÊM: ESP32 xử lý + sceneDoneHandler.py

**Step 7: TTS có audio → STOP scene + PLAY REMOTE**
- TTS provider nhận được text đầu tiên
- Server gửi stop_scene cho ESP32
- ESP32 đợi local audio phát XONG (không ngắt đột ngột)
- ESP32 gửi reply scene_done
- Server gửi Opus audio chunks
- ESP32 phát remote audio
- ✅ CẦN THÊM: connection.py + TTS provider

---

## 📋 Remote Audio Buffer (Server Buffer - Cách 1)

### Nguyên Tắc:

| Vị trí | Xử lý |
|--------|-------|
| **Server** | TTS generate → Buffer queue → ĐỢI scene_done → Gửi TẤT CẢ |
| **ESP32** | Nhận audio → Phát (không cần buffer) |

### Luồng Chi Tiết:

```
T0: Server gửi acknowledgment → ESP32 bắt đầu phát
T0-T1: ASR + LLM + TTS chạy song song
T1: TTS generate chunk 1 → SERVER BUFFER (queue)
T2: TTS generate chunk 2 → SERVER BUFFER (queue)
T3: TTS generate chunk 3 → SERVER BUFFER (queue)
...
Tn: TTS generate chunk N → SERVER BUFFER (queue) [TẤT CẢ đã buffer]
Tn+1: Acknowledgment phát xong → ESP32 gửi scene_done("acknowledgment_done")
Tn+2: Server nhận scene_done → Check remote_audio_ready
      → Gửi stop_scene + TẤT CẢ buffered chunks (lần lượt theo thứ tự)
Tn+3: ESP32 phát remote audio (theo thứ tự, không đè)
```

### Hai Trường Hợp:

#### Trường Hợp 1: TTS có audio TRƯỚC khi acknowledgment/2 phát xong

```
Server: TTS có audio → Đánh dấu remote_audio_ready = True
        → Tiếp tục buffer các chunks tiếp theo
        → ĐỢI scene_done từ ESP32
        → Khi nhận scene_done → Gửi stop_scene + TẤT CẢ audio
```

#### Trường Hợp 2: TTS có audio SAU khi acknowledgment/2 phát xong

```
Server: Nhận scene_done("acknowledgment_done")
        → Kiểm tra remote_audio_ready = True?
        → Nếu có → Gửi stop_scene + audio NGAY (không cần buffer thêm)
        → Nếu không → Gửi pending (sau 1s)
```

### Đảm Bảo Thứ Tự:

| # | Điều |
|---|------|
| 1 | **Không đè** - Mỗi chunk có sequence number |
| 2 | **Phát theo thứ tự** - Queue FIFO (First In First Out) |
| 3 | **Không mất chunk** - Tất cả được buffer trước khi gửi |
| 4 | **Giữ nguyên thứ tự** - Server gửi đúng thứ tự TTS generate |

---

## 📝 Message Types

### 1. Server → ESP32: Play Scene + Gesture
```json
{
  "type": "device_action",
  "action": "play_scene",
  "scene_tag": "acknowledgment_01",
  "emoji": "thinking",
  "gesture": "nod",
  "reply": true,
  "reply_action": "acknowledgment_done",
  "req_id": "abc123"
}
```

### 2. Server → ESP32: Change Emoji (không audio)
```json
{
  "type": "device_action",
  "action": "change_emoji",
  "emoji": "waiting",
  "req_id": "abc123"
}
```

### 3. Server → ESP32: Gesture Only (không audio)
```json
{
  "type": "device_action",
  "action": "gesture",
  "gesture": "raise_hand",
  "req_id": "abc123"
}
```

### 4. Server → ESP32: Stop Scene
```json
{
  "type": "device_action",
  "action": "stop_scene",
  "emoji": "remote",
  "gesture": "stop",
  "req_id": "abc123"
}
```

### 5. ESP32 → Server: Scene Done
```json
{
  "type": "scene_done",
  "reply_action": "acknowledgment_done",
  "req_id": "abc123"
}
```

---

## 📋 Gesture Mapping (STM32 I2C)

```
┌─────────────────────────────────────────────────────────────────┐
│ ESP32 → STM32: I2C 2 bytes                                  │
│                                                           │
│ Byte 1: 0x00 (Register config)                             │
│ Byte 2: Command hex                                       │
│                                                           │
│ Bảng lệnh:                                                │
│ ┌────────┬──────────────────┬────────────────────────────┐  │
│ │ Hex    │ Tên             │ Ý nghĩa                  │  │
│ ├────────┼──────────────────┼────────────────────────────┤  │
│ │ 0x00  │ STOP           │ Dừng tất cả             │  │
│ │ 0x01  │ FORWARD_SLOW   │ Đi tiến chậm            │  │
│ │ 0x02  │ FORWARD_FAST   │ Đi tiến nhanh           │  │
│ │ 0x03  │ BACKWARD_SLOW  │ Đi lùi chậm             │  │
│ │ 0x04  │ BACKWARD_FAST  │ Đi lùi nhanh           │  │
│ │ 0x05  │ TURN_LEFT_SLOW │ Rẽ trái chậm            │  │
│ │ 0x06  │ TURN_LEFT_FAST │ Rẽ trái nhanh          │  │
│ │ 0x07  │ TURN_RIGHT_SLOW│ Rẽ phải chậm           │  │
│ │ 0x08  │ TURN_RIGHT_FAST│ Rẽ phải nhanh          │  │
│ │ 0x09  │ SPIN_LEFT     │ Xoay trái tại chỗ       │  │
│ │ 0x0A  │ NOD          │ Gật đầu                │  │
│ │ 0x0B  │ SHAKE_HEAD    │ Lắc đầu                │  │
│ │ 0x0C  │ RAISE_HAND   │ Giơ tay                │  │
│ │ 0x0D  │ WAVE        │ Vẫy tay                │  │
│ └────────┴──────────────────┴────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📁 SD Card Structure

```
/sdcard/
├── audio/              # Audio files tĩnh
│   ├── ack_01.wav     # Acknowledgment - "Ừm." (~300ms)
│   ├── ack_02.wav     # Acknowledgment - "Ok nè." (~300ms)
│   ├── ack_03.wav     # Acknowledgment - "Để mình xem." (~400ms)
│   ├── wait_01.wav    # Pending - "Chờ mình chút nha." (~500ms)
│   ├── wait_02.wav    # Pending - "Mình đang xem đây." (~500ms)
│   └── wait_03.wav    # Pending - "Đợi xíu nè." (~400ms)
└── emoji/            # Emoji files tĩnh
    ├── thinking      # Emoji đang xử lý
    ├── waiting       # Emoji chờ lâu
    └── remote       # Emoji khi remote ready
```

---

## ⚙️ Config Parameters

### config.yaml
```yaml
# Audio Waiting Flow
waiting:
  # VAD settings
  silence_threshold_ms: 500      # Thời gian silence để trigger
  acknowledgment_send_delay_ms: 0        # Delay trước khi gửi acknowledgment
  
  # Audio tags (random chọn)
  acknowledgment_tags: ["ack_01", "ack_02", "ack_03"]
  pending_audio_tags: ["wait_01", "wait_02", "wait_03"]
  
  # Delays
  pending_delay_ms: 1000          # Delay sau acknowledgment để gửi pending
  
  # Emoji tags
  emoji_thinking: "thinking"
  emoji_waiting: "waiting"
  emoji_remote: "remote"
  
  # Gesture settings
  gesture_mapping:
    nod: 0x0A
    shake_head: 0x0B
    raise_hand: 0x0C
    wave: 0x0D
    stop: 0x00
```

---

## 📋 Các File Cần Sửa

### Tổng Quan

| # | File | Điểm Cần Sửa |
|---|------|-------------|
| 1 | `sendAudioHandle.py` | Thêm `send_device_action()`, `handle_scene_done()` |
| 2 | `receiveAudioHandle.py` | Gửi acknowledgment khi VAD silence |
| 3 | `silero.py` | Bắt `client_voice_stop` event |
| 4 | `connection.py` | Gửi stop khi TTS ready |
| 5 | `config.yaml` | Thêm waiting config |

---

## 📁 File 1: sendAudioHandle.py

**Đường dẫn:** `main/xiaozhi-server/core/handle/sendAudioHandle.py`

**Mục đích:** Gửi các device actions (play_scene, stop_scene, change_emoji, gesture) từ server xuống ESP32

**Thêm mới:** Cuối file (~line 320)

```python
import random  # Thêm vào đầu file nếu chưa có


async def send_device_action(
    conn: "ConnectionHandler",
    action: str,
    scene_tag: str = None,
    emoji: str = None,
    gesture: str = None,
    reply: bool = False,
    reply_action: str = None,
    req_id: str = None
):
    """Gửi device action cho ESP32
    
    Args:
        conn: Connection handler
        action: Loại action (play_scene, stop_scene, change_emoji, gesture)
        scene_tag: Tag audio file trên SD (VD: "ack_01", "wait_01")
        emoji: Tag emoji trên SD (VD: "thinking", "waiting", "remote")
        gesture: Tag gesture (VD: "nod", "shake_head", "stop")
        reply: ESP32 có reply khi xong không
        reply_action: Action để reply (VD: "acknowledgment_done", "pending_done")
        req_id: Request ID để tracking
    """
    message = {
        "type": "device_action",
        "action": action,
        "req_id": req_id or conn.session_id
    }
    if scene_tag:
        message["scene_tag"] = scene_tag
    if emoji:
        message["emoji"] = emoji
    if gesture:
        message["gesture"] = gesture
    if reply:
        message["reply"] = True
        message["reply_action"] = reply_action
    
    await conn.websocket.send(json.dumps(message))
    conn.logger.bind(tag=TAG).info(f"Sent device_action: {action}, scene={scene_tag}, emoji={emoji}, gesture={gesture}")
```

**Tác dụng trong luồng:**
- Step 2: Gửi acknowledgment (`action: play_scene`, `scene_tag: ack_01`, `emoji: thinking`, `gesture: nod`)
- Step 4: Gửi change emoji (`action: change_emoji`, `emoji: waiting`)
- Step 5: Gửi pending (`action: play_scene`, `scene_tag: wait_01`, `emoji: waiting`, `gesture: shake_head`)
- Step 7: Gửi stop scene (`action: stop_scene`, `emoji: remote`, `gesture: stop`)

---

## 📁 File 2: sceneDoneHandler.py (MỚI)

**Đường dẫn:** `main/xiaozhi-server/core/handle/textHandler/sceneDoneHandler.py`

**Mục đích:** Nhận message `scene_done` từ ESP32 báo hiệu audio local đã phát xong

**Tạo file mới:**

```python
import asyncio
import random
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from core.connection import ConnectionHandler

TAG = __name__


async def handle_scene_done(conn: "ConnectionHandler", data: dict):
    """Xử lý scene_done từ ESP32
    
    Khi ESP32 phát audio xong, nó sẽ gửi message:
    {
        "type": "scene_done",
        "reply_action": "acknowledgment_done" | "pending_done",
        "req_id": "abc123"
    }
    
    Server sẽ:
    - Nếu reply_action="acknowledgment_done": Check remote ready, hoặc schedule pending
    - Nếu reply_action="pending_done": Chờ remote ready
    """
    reply_action = data.get("reply_action")
    req_id = data.get("req_id", "")
    
    conn.logger.bind(tag=TAG).info(f"Received scene_done: reply_action={reply_action}, req_id={req_id}")
    
    # Lấy config
    pending_delay_ms = conn.config.get("waiting.pending_delay_ms", 1000)
    pending_audio_tags = conn.config.get("waiting.pending_audio_tags", ["wait_01"])
    
    if reply_action == "acknowledgment_done":
        # Acknowledgment xong - check remote ready
        if conn.remote_audio_ready:
            # Remote đã sẵn sàng → phát luôn
            conn.logger.bind(tag=TAG).info("Remote audio ready, playing remote")
            await send_stop_and_play_remote(conn)
        else:
            # Chưa ready → đợi 1s rồi gửi pending
            conn.waiting_for_remote = True
            conn.logger.bind(tag=TAG).info(f"Scheduling pending after {pending_delay_ms}ms")
            asyncio.create_task(_schedule_pending(conn, pending_delay_ms, pending_audio_tags))
            
    elif reply_action == "pending_done":
        # Pending xong - chờ remote
        if conn.remote_audio_ready:
            await send_stop_and_play_remote(conn)
        else:
            # Tiếp tục chờ remote (visual waiting)
            conn.logger.bind(tag=TAG).info("Waiting for remote audio")


async def _schedule_pending(conn: "ConnectionHandler", delay_ms: int, pending_audio_tags: list):
    """Gửi pending sau delay
    
    Args:
        conn: Connection handler
        delay_ms: Thời gian delay trước khi gửi (mặc định 1000ms)
        pending_audio_tags: Danh sách tags pending (random chọn)
    """
    # Đợi delay
    await asyncio.sleep(delay_ms / 1000.0)
    
    # Skip nếu remote đã sẵn sàng
    if conn.remote_audio_ready:
        conn.logger.bind(tag=TAG).info("Skipping pending - remote already ready")
        return
    
    # Skip nếu đã có audio1_sent (tránh gửi trùng)
    if not conn.waiting_acknowledgment_sent:
        conn.logger.bind(tag=TAG).info("Skipping pending - session ended")
        return
    
    # Random chọn pending
    scene_tag = random.choice(pending_audio_tags)
    
    conn.logger.bind(tag=TAG).info(f"Sending pending: {scene_tag}")
    
    # Gửi pending
    from core.handle.sendAudioHandle import send_device_action
    await send_device_action(
        conn, 
        action="play_scene", 
        scene_tag=scene_tag, 
        emoji="waiting", 
        gesture="shake_head",
        reply=True, 
        reply_action="pending_done"
    )


async def send_stop_and_play_remote(conn: "ConnectionHandler"):
    """Gửi stop scene và chuẩn bị phát remote audio
    
    Args:
        conn: Connection handler
    """
    # Gửi stop scene trước
    from core.handle.sendAudioHandle import send_device_action
    await send_device_action(
        conn, 
        action="stop_scene", 
        emoji="remote", 
        gesture="stop"
    )
    
    # Đánh dấu remote đã được phát
    conn.remote_audio_ready = False
    conn.logger.bind(tag=TAG).info("Sent stop_scene, remote audio will play")
```

**Đăng ký handler:**

Thêm vào `main/xiaozhi-server/core/handle/textMessageHandlerRegistry.py`:

```python
# Thêm import
from core.handle.textHandler.sceneDoneHandler import handle_scene_done

# Thêm vào registry
register_handler("scene_done", handle_scene_done)
```

**Tác dụng trong luồng:**
- Step 3: Nhận `scene_done("acknowledgment_done")` → Check remote hoặc schedule pending
- Step 6: Nhận `scene_done("pending_done")` → Check remote

---

## 📁 File 3: receiveAudioHandle.py

**Đường dẫn:** `main/xiaozhi-server/core/handle/receiveAudioHandle.py`

**Mục đích:** Khi VAD detect silence (user dứt lời), gửi acknowledgment cho ESP32 trước khi ASR xử lý

**Thêm/Sửa:**

### 3.1. Thêm import và biến toàn cục (đầu file)

```python
# Thêm vào đầu file sau các import
from core.handle.sendAudioHandle import send_device_action
```

### 3.2. Sửa handleAudioMessage() (line ~17-34)

**Tìm:**
```python
async def handleAudioMessage(conn: "ConnectionHandler", audio):
    # 当前片段是否有人说话
    have_voice = conn.vad.is_vad(conn, audio)
    # 如果设备刚刚被唤醒，短暂忽略VAD检测
    if hasattr(conn, "just_woken_up") and conn.just_woken_up:
        have_voice = False
        # 设置一个短暂延迟后恢复VAD检测
        if not hasattr(conn, "vad_resume_task") or conn.vad_resume_task.done():
            conn.vad_resume_task = asyncio.create_task(resume_vad_detection(conn))
        return
    # manual 模式下不打断正在播放的内容
    if have_voice:
        if conn.client_is_speaking and conn.client_listen_mode != "manual":
            await handleAbortMessage(conn)
    # 设备长时间空闲检测，用于say goodbye
    await no_voice_close_connect(conn, have_voice)
    # 接收音频
    await conn.asr.receive_audio(conn, audio, have_voice)
```

**Thay bằng:**
```python
async def handleAudioMessage(conn: "ConnectionHandler", audio):
    # 当前片段是否有人说话
    have_voice = conn.vad.is_vad(conn, audio)
    # 如果设备刚刚被唤醒，短暂忽略VAD检测
    if hasattr(conn, "just_woken_up") and conn.just_woken_up:
        have_voice = False
        # 设置一个短暂延迟后恢复VAD检测
        if not hasattr(conn, "vad_resume_task") or conn.vad_resume_task.done():
            conn.vad_resume_task = asyncio.create_task(resume_vad_detection(conn))
        return
    # manual 模式下不打断正在播放的内容
    if have_voice:
        if conn.client_is_speaking and conn.client_listen_mode != "manual":
            await handleAbortMessage(conn)
    # 设备长时间空闲检测，用于say goodbye
    await no_voice_close_connect(conn, have_voice)
    
    # === THÊM MỚI: Kiểm tra VAD silence (user dứt lời) ===
    # Khi VAD detect silence (client_voice_stop = True), gửi acknowledgment cho ESP32
    if conn.vad.client_voice_stop and not conn.waiting_acknowledgment_sent:
        conn.vad.client_voice_stop = False  # Reset flag
        conn.waiting_acknowledgment_sent = True
        conn.remote_audio_ready = False  # Reset remote ready flag
        conn.waiting_for_remote = False
        
        # Gửi acknowledgment (trước ASR)
        asyncio.create_task(_send_acknowledgment(conn))
    # === END THÊM MỚI ===
    
    # 接收音频
    await conn.asr.receive_audio(conn, audio, have_voice)
```

### 3.3. Thêm function _send_acknowledgment() (cuối file)

```python
async def _send_acknowledgment(conn: "ConnectionHandler"):
    """Gửi acknowledgment khi VAD detect silence (trước ASR)
    
    Flow:
    1. Lấy config delay (mặc định 0ms)
    2. Random chọn acknowledgment tag
    3. Gửi device_action cho ESP32
    4. ESP32 sẽ phát audio + gửi reply khi xong
    """
    # Đợi delay config (mặc định 0ms)
    delay_ms = conn.config.get("waiting.acknowledgment_send_delay_ms", 0)
    if delay_ms > 0:
        conn.logger.bind(tag=TAG).info(f"Waiting {delay_ms}ms before sending audio1")
        await asyncio.sleep(delay_ms / 1000.0)
    
    # Random chọn acknowledgment
    acknowledgment_tags = conn.config.get("waiting.acknowledgment_tags", ["ack_01"])
    scene_tag = random.choice(acknowledgment_tags)
    
    conn.logger.bind(tag=TAG).info(f"Sending acknowledgment: {scene_tag}")
    
    # Gửi device_action cho ESP32
    await send_device_action(
        conn, 
        action="play_scene", 
        scene_tag=scene_tag, 
        emoji="thinking", 
        gesture="nod",
        reply=True, 
        reply_action="acknowledgment_done"
    )
```

**Tác dụng trong luồng:**
- **Step 2**: Khi VAD detect silence → Gửi acknowledgment (`action: play_scene`, `scene_tag: ack_01`, `emoji: thinking`, `gesture: nod`, `reply: true`)

---

## 📁 File 4: silero.py (VAD)

**Đường dẫn:** `main/xiaozhi-server/core/providers/vad/silero.py`

**Mục đích:** Reset các biến trạng thái khi bắt đầu phiên audio mới

**Thêm:** Trong hàm `_init_connection_state()` (line ~67)

**Tìm:**
```python
def _init_connection_state(self, conn):
    # ... existing code ...
```

**Thêm vào sau các conn.xxx = ...:**
```python
    # === THÊM MỚI: Audio waiting flow state ===
    conn.waiting_acknowledgment_sent = False   # Chưa gửi acknowledgment
    conn.remote_audio_ready = False    # Remote audio chưa sẵn sàng
    conn.waiting_for_remote = False    # Đang chờ remote
    # === END THÊM MỚI ===
```

**Tác dụng trong luồng:**
- Reset trạng thái khi bắt đầu phiên audio mới để chuẩn bị cho flow audio waiting

---

## 📁 File 5: connection.py

**Đường dẫn:** `main/xiaozhi-server/core/connection.py`

**Mục đích:** Khi TTS có audio đầu tiên (FIRST sentence), đánh dấu remote audio sẵn sàng và gửi stop_scene cho ESP32

**Thêm:** Trong hàm `chat()` - khi put FIRST message vào TTS queue (line ~842-850)

**Tìm:**
```python
# 为最顶层时新建会话ID和发送FIRST请求
if depth == 0:
    self.sentence_id = str(uuid.uuid4().hex)
    self.dialogue.put(Message(role="user", content=query))
    self.tts.tts_text_queue.put(
        TTSMessageDTO(
            sentence_id=self.sentence_id,
            sentence_type=SentenceType.FIRST,
            content_type=ContentType.ACTION,
        )
    )
```

**Thêm vào sau:**
```python
    # === THÊM MỚI: Audio waiting flow ===
    # Khi TTS bắt đầu (FIRST sentence), đánh dấu remote ready
    if self.waiting_for_remote:
        self.remote_audio_ready = True
        # Gửi stop_scene cho ESP32 để chuẩn bị phát remote
        from core.handle.sendAudioHandle import send_device_action
        asyncio.create_task(send_device_action(
            self, 
            action="stop_scene", 
            emoji="remote", 
            gesture="stop"
        ))
        self.logger.bind(tag=TAG).info("Remote audio ready - sent stop_scene to ESP32")
    # === END THÊM MỚI ===
```

**Tác dụng trong luồng:**
- **Step 7**: Khi TTS có audio → Đánh dấu `remote_audio_ready = True` + Gửi `stop_scene` cho ESP32

---

## 📁 File 6: Config (data/.config.yaml)

**Đường dẫn:** `main/xiaozhi-server/data/.config.yaml`

**Mục đích:** Thêm cấu hình cho audio waiting flow

**Thêm mới:**

```yaml
# =============================================
# Audio Waiting Flow Configuration
# =============================================
# Audio waiting flow: Giảm khoảng trống khi user nói xong đến lúc có audio trả lời
# Luồng: Acknowledgment → ASR/LLM → Pending (nếu cần) → Remote Audio
waiting:
  # ===== Acknowledgment Settings =====
  # Delay trước khi gửi acknowledgment (ms), 0 = gửi ngay khi VAD silence
  acknowledgment_send_delay_ms: 0
  
  # Danh sách acknowledgment tags (sẽ random chọn 1)
  acknowledgment_tags:
    - "ack_01"
    - "ack_02"
    - "ack_03"
  
  # ===== Pending Settings =====
  # Delay sau khi acknowledgment kết thúc trước khi gửi pending (ms)
  pending_delay_ms: 1000
  
  # Danh sách pending tags (sẽ random chọn 1)
  pending_audio_tags:
    - "wait_01"
    - "wait_02"
    - "wait_03"
  
  # ===== Emoji Settings =====
  # Các emoji tags (ESP32 sẽ load từ SD card)
  emoji_thinking: "thinking"    # Emoji khi đang xử lý (acknowledgment)
  emoji_waiting: "waiting"     # Emoji khi chờ lâu (pending)
  emoji_remote: "remote"       # Emoji khi remote audio sẵn sàng
  
  # ===== Gesture Settings =====
  # Gesture mapping (ESP32 sẽ map sang hex cho STM32)
  # Server gửi string tag, ESP32 tự map sang hex
  gesture_nod: "nod"           # Gật đầu
  gesture_shake_head: "shake_head"  # Lắc đầu
  gesture_stop: "stop"          # Dừng
  # Các gestures khác nếu cần:
  # gesture_raise_hand: "raise_hand"
  # gesture_wave: "wave"
```

---

## 📁 File 7: textMessageHandlerRegistry.py

**Đường dẫn:** `main/xiaozhi-server/core/handle/textMessageHandlerRegistry.py`

**Mục đích:** Đăng ký handler cho message type `scene_done` từ ESP32

**Thêm:**

### 7.1. Thêm import

```python
# Thêm vào đầu file
from core.handle.textHandler.sceneDoneHandler import handle_scene_done
```

### 7.2. Thêm vào registry

```python
# Thêm vào hàm đăng ký hoặc cuối file
register_handler("scene_done", handle_scene_done)
```

**Tác dụng trong luồng:**
- Khi ESP32 gửi `{"type": "scene_done", ...}` → Gọi `handle_scene_done()`

---

## 📊 Tổng Kết Files Cần Sửa

| # | File | Action | Mô tả |
|---|------|--------|-------|
| 1 | `sendAudioHandle.py` | Thêm | Function `send_device_action()` |
| 2 | `sceneDoneHandler.py` | Tạo mới | Handler cho `scene_done` message |
| 3 | `receiveAudioHandle.py` | Thêm/Sửa | Gửi acknowledgment khi VAD silence |
| 4 | `silero.py` | Thêm | Reset state variables |
| 5 | `connection.py` | Thêm | Đánh dấu remote ready khi TTS ready |
| 6 | `config.yaml` | Thêm | Waiting flow config |
| 7 | `textMessageHandlerRegistry.py` | Thêm | Đăng ký scene_done handler |

---

## 📊 Tổng Kết Dòng Code

| # | File | Thêm | Sửa |
|---|------|------|------|
| 1 | `sendAudioHandle.py` | +30 | 0 |
| 2 | `sceneDoneHandler.py` | +110 | 0 |
| 3 | `receiveAudioHandle.py` | +25 | +10 |
| 4 | `silero.py` | +5 | 0 |
| 5 | `connection.py` | +15 | 0 |
| 6 | `config.yaml` | +35 | 0 |
| 7 | `textMessageHandlerRegistry.py` | +5 | 0 |
| | **Tổng** | **~225** | **~10** |

---

## ⚠️ ESP32 Implementation Guide

### Overview

ESP32 cần xử lý các messages từ server và gửi reply khi hoàn thành.

### 1. Nhận Message Từ Server

Khi nhận được message từ WebSocket, ESP32 parse JSON:

```c
void handleWebSocketMessage(String message) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, message);
    
    String type = doc["type"];
    
    if (type == "device_action") {
        String action = doc["action"];
        String scene_tag = doc["scene_tag"] | "";
        String emoji = doc["emoji"] | "";
        String gesture = doc["gesture"] | "";
        bool reply = doc["reply"] | false;
        String reply_action = doc["reply_action"] | "";
        
        // Xử lý action
        if (action == "play_scene") {
            // 1. Play audio từ SD card
            // 2. Thực hiện gesture (gửi I2C)
            // 3. Đổi emoji
            // 4. Nếu reply=true, gửi scene_done khi xong
            handlePlayScene(scene_tag, emoji, gesture, reply, reply_action);
        }
        else if (action == "change_emoji") {
            // Chỉ đổi emoji
            changeEmoji(emoji);
        }
        else if (action == "gesture") {
            // Chỉ thực hiện gesture
            sendGestureToSTM32(gesture);
        }
        else if (action == "stop_scene") {
            // Dừng scene + đổi emoji + gesture stop
            stopScene();
            changeEmoji(emoji);
            sendGestureToSTM32(gesture);
        }
    }
}
```

### 2. Play Scene (Audio + Emoji + Gesture)

```c
void handlePlayScene(String scene_tag, String emoji, String gesture, bool reply, String reply_action) {
    // 1. Gửi gesture sang STM32 (I2C)
    if (gesture != "") {
        sendGestureToSTM32(gesture);
    }
    
    // 2. Đổi emoji
    if (emoji != "") {
        changeEmoji(emoji);
    }
    
    // 3. Phát audio từ SD card
    String audio_file = "/audio/" + scene_tag + ".wav";
    playAudioFile(audio_file);
    
    // 4. Nếu reply=true, gửi scene_done khi audio xong
    if (reply) {
        waitForAudioComplete();
        sendSceneDone(reply_action);
    }
}
```

### 3. Gửi Gesture Qua I2C (ESP32 → STM32)

```c
// Gesture mapping (ESP32 local)
const char* GESTURE_MAP[] = {
    "stop",        // 0x00
    "forward_slow", // 0x01
    "forward_fast", // 0x02
    "backward_slow", // 0x03
    "backward_fast", // 0x04
    "turn_left_slow", // 0x05
    "turn_left_fast", // 0x06
    "turn_right_slow", // 0x07
    "turn_right_fast", // 0x08
    "spin_left",    // 0x09
    "nod",          // 0x0A
    "shake_head",   // 0x0B
    "raise_hand",   // 0x0C
    "wave"          // 0x0D
};

uint8_t gestureToHex(const char* gesture) {
    if (strcmp(gesture, "stop") == 0) return 0x00;
    if (strcmp(gesture, "forward_slow") == 0) return 0x01;
    if (strcmp(gesture, "forward_fast") == 0) return 0x02;
    if (strcmp(gesture, "backward_slow") == 0) return 0x03;
    if (strcmp(gesture, "backward_fast") == 0) return 0x04;
    if (strcmp(gesture, "turn_left_slow") == 0) return 0x05;
    if (strcmp(gesture, "turn_left_fast") == 0) return 0x06;
    if (strcmp(gesture, "turn_right_slow") == 0) return 0x07;
    if (strcmp(gesture, "turn_right_fast") == 0) return 0x08;
    if (strcmp(gesture, "spin_left") == 0) return 0x09;
    if (strcmp(gesture, "nod") == 0) return 0x0A;
    if (strcmp(gesture, "shake_head") == 0) return 0x0B;
    if (strcmp(gesture, "raise_hand") == 0) return 0x0C;
    if (strcmp(gesture, "wave") == 0) return 0x0D;
    return 0x00; // default: stop
}

void sendGestureToSTM32(const char* gesture) {
    uint8_t cmd = gestureToHex(gesture);
    
    Wire.beginTransmission(STM32_ADDR);
    Wire.write(0x00);      // Register config
    Wire.write(cmd);       // Command
    Wire.endTransmission();
    
    Serial.printf("Sent gesture: %s (0x%02X)\n", gesture, cmd);
}
```

### 4. Gửi Scene Done Về Server

```c
void sendSceneDone(String reply_action) {
    DynamicJsonDocument doc(256);
    doc["type"] = "scene_done";
    doc["reply_action"] = reply_action;
    doc["req_id"] = current_req_id;
    
    String output;
    serializeJson(doc, output);
    webSocket.sendTXT(output);
    
    Serial.printf("Sent scene_done: %s\n", reply_action.c_str());
}
```

### 5. State Machine (Tùy Chọn)

Nếu muốn quản lý state rõ ràng hơn:

```c
enum VoiceState {
    IDLE,
    LISTENING,
    LOCAL_AUDIO_1_PLAYING,
    WAITING_REMOTE_VISUAL,
    LOCAL_AUDIO_2_PLAYING,
    SPEAKING_REMOTE
};

VoiceState currentState = IDLE;
bool remote_audio_buffered = false;

void updateState(VoiceState newState) {
    currentState = newState;
    Serial.printf("State: %d\n", newState);
}
```

---

## ✅ ESP32 Implementation Status

**File:** `main/application.cc` - ✅ IMPLEMENTED

### Đã Implement:

| # | Chức năng | Status | Code |
|---|---------|--------|------|
| 1 | `device_action` message type | ✅ | application.cc:614 |
| 2 | `play_scene` action | ✅ | HandlePlayScene() |
| 3 | `stop_scene` action | ✅ | HandleStopScene() |
| 4 | `change_emoji` action | ✅ | HandleDeviceAction() |
| 5 | `gesture` action | ✅ | HandleDeviceAction() |
| 6 | `scene_tag` field | ✅ | Parse từ JSON |
| 7 | `emoji` field | ✅ | Parse từ JSON |
| 8 | `gesture` field | ✅ | Parse từ JSON |
| 9 | `reply` field | ✅ | Parse từ JSON |
| 10 | `reply_action` field | ✅ | Parse từ JSON |
| 11 | `req_id` field | ✅ | Parse từ JSON |
| 12 | Gesture mapping (14 gestures) | ✅ | GESTURE_MAP |
| 13 | `scene_done` reply | ✅ | SendSceneDoneReply() |
| 14 | req_id trong reply | ✅ | Thêm mới |
| 15 | I2C communication | ✅ | SendGestureToSTM32() |

### Key Changes:

```c++
// main/application.h
void SendSceneDoneReply(const char* reply_action, const char* req_id = nullptr);

// main/application.cc
// 1. HandlePlayScene - thêm req_id extraction
auto req_id = cJSON_GetObjectItem(root, "req_id");
std::string req_id_str = req_id ? req_id->valuestring : "";

// 2. SendSceneDoneReply - thêm req_id vào reply
if (req_id != nullptr) {
    cJSON_AddStringToObject(root, "req_id", req_id);
}
```

### Audio/SD Card Paths:

| Loại | Path |
|------|------|
| Audio | `/sdcard/dodomio/audio/<scene>.wav` |
| Emoji | `/dodomio/emoji/<emoji>.png` |

---

## ✅ Checklist

| # | Confirm | Nội dung |
|---|---------|---------|
| 1 | ✅ | Luồng hoàn chỉnh như trên |
| 2 | ✅ | Message types đầy đủ |
| 3 | ✅ | Gesture qua I2C 2 bytes (0x00 + hex) |
| 4 | ✅ | Gesture mapping đầy đủ (14 gestures) |
| 5 | ✅ | Remote audio CHỜ local xong |
| 6 | ✅ | SD card structure có audio/ + emoji/ |
| 7 | ✅ | Config parameters configurable |
| 8 | ✅ | req_id trong scene_done reply |

## ✅ File Changes Summary

| File | Thay đổi |
|------|---------|
| `main/application.h` | +1 param trong SendSceneDoneReply() |
| `main/application.cc` | Thêm req_id extraction + truyền trong reply |
| `xiaozhi-audio-waiting-flow.md` | Thêm implementation status |