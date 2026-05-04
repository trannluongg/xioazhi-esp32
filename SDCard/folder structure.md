# Cấu trúc thư mục SD Card (SD Card Folder Structure)

Tài liệu này mô tả cấu trúc và nội dung của các thư mục trên thẻ nhớ SD cho robot Xiaozhi-AI.

## Tổng quan (Overview)

```text
SDCard/
├── audio/            # Chứa các file âm thanh (MP3, WAV)
│   ├── lession/      # Âm thanh phục vụ các bài học
│   │   ├── english/
│   │   ├── history/
│   │   └── mommy baby/
│   └── system/       # Âm thanh hệ thống (thông báo, khởi động...)
├── emoji/            # Chứa các hiệu ứng hình ảnh (GIF, MP4)
│   ├── default/      # Biểu cảm khuôn mặt theo cảm xúc mặc định của xiaozhi
│   │   ├── 1. neutral/
│   │   ├── 2. happy/
│   │   ├── ...
│   │   └── 21. confuse/
│   └── system/       # Hiệu ứng trạng thái hệ thống (sạc, kết nối, lỗi...)
├── lessons/          # Chứa tài liệu hình ảnh cho các bài học
│   ├── english/
│   ├── history/
│   └── momy baby/    # Chứa các file GIF minh họa bài học
├── system/           # Thư mục hệ thống (cấu hình, log...)
└── folder structure.md # File mô tả 
```

---

## Chi tiết các thư mục (Detailed Breakdown)

### 1. `audio/`
Thư mục gốc chứa toàn bộ dữ liệu âm thanh của robot.
- **`lessons/`**: Phân chia theo từng môn học hoặc chủ đề.
- **`system/`**: Các âm thanh thông báo của hệ thống (ví dụ: "Đã kết nối", "Pin yếu"...).

### 2. `emoji/`
Chứa các file đồ họa (chủ yếu là định dạng `.gif`) để hiển thị trên màn hình LCD của robot.
- **`default/`**: Được tổ chức thành các thư mục con theo số thứ tự và tên cảm xúc. Robot sẽ truy cập vào đây để hiển thị biểu cảm tương ứng với tâm trạng hiện tại.
  - Ví dụ: `1. neutral`, `2. happy`, `5. sad`, `6. angry`...
- **`system/`**: Chứa các file GIF mô tả trạng thái hoạt động của hệ thống.
  - `charging.gif`: Đang sạc pin.
  - `connecting.gif`: Đang kết nối mạng.
  - `System error.gif`: Lỗi hệ thống.
  - `listening.gif`: Đang lắng nghe người dùng.

### 3. `lessons/`
Chứa các tài nguyên hình ảnh hoặc video minh họa cho nội dung bài học.
- **`momy baby/`**: Chứa bộ các file GIF minh họa (ví dụ: `Câu 1.gif`, `Câu 2.gif`...) dùng để hiển thị khi robot đang giảng bài hoặc tương tác theo chủ đề này.

### 4. `system/`
Thư mục dành riêng cho các tệp tin cấu hình hoặc dữ liệu vận hành của firmware (hiện tại có thể để trống).

---

## Lưu ý khi thêm nội dung
1. **Định dạng file**: 
   - Âm thanh: Ưu tiên `.mp3` hoặc `.wav`.
   - Hình ảnh: `.gif` cho diễn hoạt, `.jpg`/`.png` cho hình tĩnh.
2. **Quy tắc đặt tên**: 
   - Nên sử dụng tiếng Anh hoặc tiếng Việt không dấu, tránh ký tự đặc biệt để đảm bảo firmware đọc file ổn định.
   - Đối với các file bài học, nên đánh số thứ tự (ví dụ: `01.mp3`, `02.mp3`) để dễ quản lý.
