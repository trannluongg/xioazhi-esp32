# Giao thức Truyền Thông I2C (Master - Slave)

Tài liệu này mô tả giao thức truyền tin qua chuẩn I2C tối giản (1-byte) giữa thiết bị điều khiển trung tâm ESP32-S3 (Master) và mạch điều khiển STM32F103 (Slave) để điều khiển động cơ di chuyển (DRV8833) và cánh tay (Servo SG90).
SDA Pin: GPIO 7
SCL Pin: GPIO 8

## 1. Thông số Kỹ thuật Cấu hình I2C

* **Chế độ hoạt động:** Master (Write) -> Slave (Read)
* **Địa chỉ I2C của Slave (STM32):** `0x20` (Hex)
* **Tốc độ truyền (Clock Speed):** Standard Mode (100 kHz)
* **Độ dài Frame tin:** Cố định **2 Bytes** mỗi lần gửi (Byte 1: Địa chỉ thanh ghi `0x00`, Byte 2: Mã lệnh lệnh).

---

## 2. Giao tiếp Thanh Ghi (Register Map)

Mỗi lần Master ra lệnh cho Robot, ESP32 sẽ gửi một Frame gồm 2 byte:
- **Byte 1:** Thanh ghi ảo (`0x00`). Lệnh nhận dạng địa chỉ Register lưu cấu hình.
- **Byte 2:** Mã lệnh (Command) ứng với bảng mã bên dưới.

STM32 sẽ nhận và thực thi ngay lập tức dựa trên thông số mặc định đã lập trình.

| Mã Lệnh (Hex) | Tên Lệnh | Ý nghĩa |
| :---: | :--- | :--- |
| `0x00` | **STOP** | Dừng lại lập tức tất cả hành động (Fast Decay/Brake) |
| `0x01` | **FORWARD_SLOW**| Đi Tiến chậm |
| `0x02` | **FORWARD_FAST**| Đi Tiến nhanh |
| `0x03` | **BACKWARD_SLOW**| Đi Lùi chậm |
| `0x04` | **BACKWARD_FAST**| Đi Lùi nhanh |
| `0x05` | **TURN_LEFT_SLOW**| Rẽ Trái chậm |
| `0x06` | **TURN_LEFT_FAST**| Rẽ Trái nhanh |
| `0x07` | **TURN_RIGHT_SLOW**| Rẽ Phải chậm |
| `0x08` | **TURN_RIGHT_FAST**| Rẽ Phải nhanh |
| `0x09` | **SPIN_LEFT** | Xoay người tại chỗ sang trái |
| `0x0A` | **SPIN_RIGHT**| Xoay người tại chỗ sang phải |
| `0x10` | **ARM_WAVE_BOTH** | Tự động vẫy cả hai tay |
| `0x11` | **ARM_WAVE_LEFT** | Tự động vẫy tay trái |
| `0x12` | **ARM_WAVE_RIGHT**| Tự động vẫy tay phải |
| `0x13` | **ARM_RAISE_BOTH**| Giơ hai tay lên cao |
| `0x14` | **ARM_RAISE_LEFT**| Giơ tay trái |
| `0x15` | **ARM_RAISE_RIGHT**| Giơ tay phải |
| `0x16` | **ARM_LOWER_BOTH**| Hạ hai tay xuống |
| `0x17` | **ARM_LOWER_LEFT**| Hạ tay trái |
| `0x18` | **ARM_LOWER_RIGHT**| Hạ tay phải |
| `0x19` | **HEAD_RIGHT**| Đầu quay sang phải |
| `0x1A` | **HEAD_LEFT**| Đầu quay sang trái |
| `0x1B` | **HEAD_SHAKE**| Lắc đầu |
| `0x1C` | **ARM_TALK_MOTION**| Cử động tay, đầu nhịp nhàng như khi đang nói chuyện |
| `0x1D` | **ACTION_1**| Định nghĩa hành động 1 |
| `0x1E` | **ACTION_2**| Định nghĩa hành động 2 |
| `0x1F` | **ACTION_3**| Định nghĩa hành động 3 |
| `0x20` | **ACTION_4**| Định nghĩa hành động 4 |
| `0x21` | **ACTION_5**| Định nghĩa hành động 5 |
| `0x22` | **ACTION_6**| Định nghĩa hành động 6 |
| `0x23` | **ACTION_7**| Định nghĩa hành động 7 |
| `0x24` | **ACTION_8**| Định nghĩa hành động 8 |
| `0x25` | **ACTION_9**| Định nghĩa hành động 9 |
| `0x26` | **ACTION_10**| Định nghĩa hành động 10 |


*Lưu ý: Các thông số như tốc độ động cơ (DRV8833), góc xoay của tay hoặc thời gian thực hiện lệnh sẽ được thiết lập mức mặc định trong chương trình của STM32 để đơn giản hoá dữ liệu truyền.*

---

## 4. Giao tiếp (Ví dụ Code trên ESP32)

Bản tin truyền đi chỉ bao gồm địa chỉ thanh ghi (`0x00`) và mã lệnh.

```c
// Code C/C++ gửi lệnh I2C bằng ESP-IDF
uint8_t payload[2];
payload[0] = 0x00; // Thanh ghi 0x00
payload[1] = 0x02; // Lệnh đi tiến tới (Nhanh)

i2c_master_transmit(i2c_dev_handle, payload, 2, portMAX_DELAY);
```