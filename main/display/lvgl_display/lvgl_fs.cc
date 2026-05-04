#include "lvgl_fs.h"
#include <lvgl.h>
#include <cstdio>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <esp_log.h>

#define TAG "LVGL_FS"
#define FS_PREFIX "/sdcard"

static void* fs_open(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode) {
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", FS_PREFIX, path);

    const char* mode_str = "";
    if (mode == LV_FS_MODE_WR) mode_str = "wb";
    else if (mode == LV_FS_MODE_RD) mode_str = "rb";
    else if (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) mode_str = "rb+";

    FILE* f = fopen(full_path, mode_str);
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", full_path);
        return nullptr;
    }
    return (void*)f;
}

static lv_fs_res_t fs_close(lv_fs_drv_t* drv, void* file_p) {
    fclose((FILE*)file_p);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_read(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br) {
    size_t res = fread(buf, 1, btr, (FILE*)file_p);
    if (br) *br = (uint32_t)res;
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_write(lv_fs_drv_t* drv, void* file_p, const void* buf, uint32_t btw, uint32_t* bw) {
    size_t res = fwrite(buf, 1, btw, (FILE*)file_p);
    if (bw) *bw = (uint32_t)res;
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_seek(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence) {
    int w = SEEK_SET;
    if (whence == LV_FS_SEEK_CUR) w = SEEK_CUR;
    else if (whence == LV_FS_SEEK_END) w = SEEK_END;

    fseek((FILE*)file_p, pos, w);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_tell(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p) {
    *pos_p = ftell((FILE*)file_p);
    return LV_FS_RES_OK;
}

void lvgl_fs_register() {
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);

    drv.letter = 'S';
    drv.cache_size = 0;

    drv.ready_cb = nullptr;
    drv.open_cb = fs_open;
    drv.close_cb = fs_close;
    drv.read_cb = fs_read;
    drv.write_cb = fs_write;
    drv.seek_cb = fs_seek;
    drv.tell_cb = fs_tell;

    drv.dir_open_cb = nullptr;
    drv.dir_read_cb = nullptr;
    drv.dir_close_cb = nullptr;

    lv_fs_drv_register(&drv);
    ESP_LOGI(TAG, "LVGL FS driver registered with letter 'S'");
}
