#ifndef SD_CARD_UTILS_H
#define SD_CARD_UTILS_H

#include <esp_err.h>
#include <dirent.h>
#include <sys/stat.h>

// Lấy đường dẫn SD card mount point
const char* GetSDCardMountPoint();

// Kiểm tra xem SD card có được mount không
bool IsSDCardMounted();

// Liệt kê các file trong thư mục (path: "dodomio/emoji")
void ListSDCardFiles(const char* path);

#endif  // SD_CARD_UTILS_H