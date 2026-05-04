#include "sd_card_utils.h"

#include <esp_log.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>

static const char* TAG = "SDCardUtils";

// Lấy đường dẫn SD card mount point
const char* GetSDCardMountPoint() {
    // Thử các mount point phổ biến
    #ifdef SDCARD_MOUNT_POINT
    return SDCARD_MOUNT_POINT;
    #elif defined(SD_MOUNT_POINT)
    return SD_MOUNT_POINT;
    #elif defined(SD_BASE_PATH)
    return SD_BASE_PATH;
    #else
    return "/sdcard";
    #endif
}

// Kiểm tra xem SD card có được mount không
bool IsSDCardMounted() {
    const char* mount_point = GetSDCardMountPoint();
    DIR* dir = opendir(mount_point);
    if (dir != nullptr) {
        closedir(dir);
        ESP_LOGI(TAG, "SD card mounted at: %s", mount_point);
        return true;
    }
    ESP_LOGW(TAG, "SD card NOT mounted at: %s", mount_point);
    return false;
}

// Liệt kê các file trong thư mục (path: "dodomio/emoji")
void ListSDCardFiles(const char* path) {
    const char* mount_point = GetSDCardMountPoint();
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", mount_point, path);
    
    ESP_LOGI(TAG, "Listing: %s", full_path);
    
    DIR* dir = opendir(full_path);
    if (dir == nullptr) {
        ESP_LOGW(TAG, "Cannot open directory: %s", full_path);
        return;
    }
    
    struct dirent* entry;
    int count = 0;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] != '.') {
            ESP_LOGI(TAG, "  [%d] %s", count++, entry->d_name);
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "Total: %d files", count);
}