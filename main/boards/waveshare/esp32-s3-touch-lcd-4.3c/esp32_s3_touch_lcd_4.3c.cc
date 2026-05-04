#include "wifi_board.h"
#include "display/lcd_display.h"

#include "codecs/box_audio_codec.h"
#include "application.h"
#include "button.h"
#include "mcp_server.h"
#include "config.h"
#include "power_save_timer.h"
#include "i2c_device.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>

#include <esp_lcd_touch_gt911.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

#include "display/lvgl_display/lvgl_theme.h"

#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <esp_random.h>

#define TAG "WaveshareEsp32s3TouchLCD43c"

class CustomBacklight : public Backlight {
public:
    CustomBacklight(esp_io_expander_handle_t io_handle)
        : Backlight(), io_handle_(io_handle) {}

protected:
    esp_io_expander_handle_t io_handle_;

    virtual void SetBrightnessImpl(uint8_t brightness) override {
        if (brightness > 100) brightness = 100;
        int flipped_brightness = 100 - brightness;

        custom_io_expander_set_pwm(io_handle_, flipped_brightness * 255 / 100);
    }
};


class WaveshareEsp32s3TouchLCD43c : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    LcdDisplay* display_;
    esp_io_expander_handle_t io_expander = NULL;
    PowerSaveTimer* power_save_timer_;
    CustomBacklight *backlight_;
    bool is_sdcard_found_ = false;

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(10); });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();});
        power_save_timer_->SetEnabled(true);
    }

    void InitializeGpio() {
        // Zero-initialize the GPIO configuration structure
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE; // Disable interrupts for this pin
        io_conf.pin_bit_mask = 1ULL << BSP_LCD_TOUCH_INT;    // Select the GPIO pin using a bitmask
        io_conf.mode = GPIO_MODE_OUTPUT;          // Set pin as output
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE; // Disable pull-up
        gpio_config(&io_conf); // Apply the configuration
    }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = BSP_I2C_SDA,
            .scl_io_num = BSP_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeCustomio(void) {
        custom_io_expander_new_i2c_ch32v003(i2c_bus_, BSP_IO_EXPANDER_I2C_ADDRESS, &io_expander);
        esp_io_expander_set_dir(io_expander, BSP_POWER_AMP_IO | BSP_LCD_BACKLIGHT | BSP_LCD_TOUCH_RST | IO_EXPANDER_PIN_NUM_4, IO_EXPANDER_OUTPUT);
        esp_io_expander_set_level(io_expander, BSP_POWER_AMP_IO | BSP_LCD_BACKLIGHT | BSP_LCD_TOUCH_RST | IO_EXPANDER_PIN_NUM_4, 1);

        esp_io_expander_set_level(io_expander, BSP_LCD_TOUCH_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(BSP_LCD_TOUCH_INT, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_io_expander_set_level(io_expander, BSP_LCD_TOUCH_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    void InitializeRGB() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel_handle = nullptr;

        esp_lcd_rgb_panel_config_t rgb_config = {
            .clk_src = LCD_CLK_SRC_DEFAULT,
            .timings = {
                .pclk_hz = 16 * 1000 * 1000,
                .h_res = BSP_LCD_H_RES,
                .v_res = BSP_LCD_V_RES,
                .hsync_pulse_width = 4,
                .hsync_back_porch = 4,
                .hsync_front_porch = 8,
                .vsync_pulse_width = 4,
                .vsync_back_porch = 4,
                .vsync_front_porch = 8,
                .flags = {
                    .pclk_active_neg = true
                }
            },
            .data_width = 16,
            .bits_per_pixel = 16,
            .num_fbs = 2,
            .bounce_buffer_size_px = BSP_LCD_H_RES * 10,
            .psram_trans_align = 64,
            .hsync_gpio_num = BSP_LCD_HSYNC,
            .vsync_gpio_num = BSP_LCD_VSYNC,
            .de_gpio_num = BSP_LCD_DE,
            .pclk_gpio_num = BSP_LCD_PCLK,
            .disp_gpio_num = BSP_LCD_DISP,
            .data_gpio_nums = {
                BSP_LCD_DATA0, BSP_LCD_DATA1, BSP_LCD_DATA2, BSP_LCD_DATA3,
                BSP_LCD_DATA4, BSP_LCD_DATA5, BSP_LCD_DATA6, BSP_LCD_DATA7,
                BSP_LCD_DATA8, BSP_LCD_DATA9, BSP_LCD_DATA10, BSP_LCD_DATA11,
                BSP_LCD_DATA12, BSP_LCD_DATA13, BSP_LCD_DATA14, BSP_LCD_DATA15
            },
            .flags = {
                .fb_in_psram = 1,
            },
        };

        ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&rgb_config, &panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

        display_ = new RgbLcdDisplay(panel_io, panel_handle,
                                  BSP_LCD_H_RES, BSP_LCD_V_RES, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                  DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);

        backlight_ = new CustomBacklight(io_expander);
        backlight_->RestoreBrightness();                    
    }

    void InitializeTouch() {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = BSP_LCD_H_RES - 1,
            .y_max = BSP_LCD_V_RES - 1,
            .rst_gpio_num = GPIO_NUM_NC,
            .int_gpio_num = GPIO_NUM_NC,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        // esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        // tp_io_config.scl_speed_hz = 400 * 1000;
         esp_lcd_panel_io_i2c_config_t tp_io_config = {
            .on_color_trans_done = NULL,
            .user_ctx = NULL,
            .control_phase_bytes = 1,
            .dc_bit_offset = 0,
            .lcd_cmd_bits = 16,
            .lcd_param_bits = 16,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 1,
            }
        };
        tp_io_config.scl_speed_hz = 400000;
        tp_io_config.dev_addr = 0x5D;

        esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle);

        ESP_LOGI(TAG, "Initialize touch controller");
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp));
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = tp,
        };
        lvgl_port_add_touch(&touch_cfg);
        ESP_LOGI(TAG, "Touch panel initialized successfully");
    }

    // Initialization tool
    void InitializeTools() {
        auto &mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.system.reconfigure_wifi",
            "Reboot the device and enter WiFi configuration mode.\n"
            "**CAUTION** You must ask the user to confirm this action.",
            PropertyList(), [this](const PropertyList& properties) {
                EnterWifiConfigMode();
                return true;
            });
    }

    void InitializeSdCard() {
        ESP_LOGI(TAG, "========== SD Card Detection ==========");
        ESP_LOGI(TAG, "Scanning for SD card...");
        ESP_LOGI(TAG, "  SD CLK: GPIO%d", BSP_SD_CLK);
        ESP_LOGI(TAG, "  SD CMD: GPIO%d", BSP_SD_CMD);
        ESP_LOGI(TAG, "  SD D0 : GPIO%d", BSP_SD_D0);

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 16 * 1024,
        };

        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.flags = SDMMC_HOST_FLAG_1BIT;  // Use 1-bit bus width
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;

        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.width = 1;  // 1-bit bus width
        slot_config.clk = BSP_SD_CLK;
        slot_config.cmd = BSP_SD_CMD;
        slot_config.d0  = BSP_SD_D0;
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        sdmmc_card_t* card = NULL;
        esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);

        if (ret != ESP_OK) {
            is_sdcard_found_ = false;
            if (ret == ESP_FAIL) {
                ESP_LOGE(TAG, "[SD] FAILED: Unable to mount filesystem on SD card");
            } else {
                ESP_LOGE(TAG, "[SD] NOT PLUGGED or init failed: %s", esp_err_to_name(ret));
            }
            ESP_LOGW(TAG, "[SD] SD card is NOT detected. Emoji from SD card will not be available.");
        } else {
            is_sdcard_found_ = true;
            ESP_LOGI(TAG, "[SD] PLUGGED - SD card mounted successfully at %s", SD_MOUNT_POINT);
            sdmmc_card_print_info(stdout, card);

            // Check /sdcard/dodomio/emoji directory
            struct stat st;
            if (stat("/sdcard/dodomio/emoji", &st) == 0 && S_ISDIR(st.st_mode)) {
                ESP_LOGI(TAG, "[SD] Found /sdcard/dodomio/emoji directory");
                ListSdDirectory("/sdcard/dodomio/emoji");
            } else {
                ESP_LOGW(TAG, "[SD] /sdcard/dodomio/emoji directory NOT found");
                // List root to help user debug
                ListSdDirectory("/sdcard");
            }
        }
        ESP_LOGI(TAG, "========================================");
    }

    void ListSdDirectory(const char* path, int level = 0) {
        DIR* dir = opendir(path);
        if (!dir) {
            ESP_LOGE(TAG, "Failed to open directory: %s", path);
            return;
        }
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            // Print indentation
            char indent[64] = {0};
            for (int i = 0; i < level * 2 && i < 62; i++) indent[i] = ' ';
            
            if (entry->d_type == DT_DIR) {
                ESP_LOGI(TAG, "%s[DIR] %s", indent, entry->d_name);
                std::string next = std::string(path) + "/" + entry->d_name;
                ListSdDirectory(next.c_str(), level + 1);
            } else {
                ESP_LOGI(TAG, "%s%s", indent, entry->d_name);
            }
        }
        closedir(dir);
    }

    void PlayRandomEmojiFromSdCard() {
        if (!is_sdcard_found_) {
            ESP_LOGW(TAG, "SD card not found, skipping emoji playback");
            return;
        }

        const char* emoji_path = "/sdcard/dodomio/emoji";
        struct stat st;
        if (stat(emoji_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
            ESP_LOGW(TAG, "Emoji directory not found: %s", emoji_path);
            return;
        }

        // Collect all .gif files in the emoji directory
        DIR* dir = opendir(emoji_path);
        if (!dir) {
            ESP_LOGE(TAG, "Failed to open emoji directory: %s", emoji_path);
            return;
        }

        std::vector<std::string> gif_files;
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            std::string name = entry->d_name;
            // Check for .gif extension (case insensitive)
            if (name.size() > 4) {
                std::string ext = name.substr(name.size() - 4);
                if (ext == ".gif" || ext == ".GIF") {
                    gif_files.push_back(name);
                }
            }
        }
        closedir(dir);

        if (gif_files.empty()) {
            ESP_LOGW(TAG, "No .gif files found in %s", emoji_path);
            return;
        }

        // Pick a random emoji
        int index = esp_random() % gif_files.size();
        std::string selected = gif_files[index];
        ESP_LOGI(TAG, "Playing random emoji from SD: %s (picked %d of %d)",
                 selected.c_str(), index + 1, (int)gif_files.size());

        // Extract name without extension for SetEmotion
        std::string emoji_name = selected.substr(0, selected.size() - 4);
        std::string full_lv_path = "S:/dodomio/emoji/" + selected;
        ESP_LOGI(TAG, "Emoji LVGL path: %s", full_lv_path.c_str());
        
        auto display = GetDisplay();
        if (display) {
            auto theme = static_cast<LvglTheme*>(display->GetTheme());
            if (theme && theme->emoji_collection()) {
                theme->emoji_collection()->AddEmoji(emoji_name, new LvglFileImage(full_lv_path));
            }
            display->SetEmotion(emoji_name.c_str());
        }
    }

public:
    WaveshareEsp32s3TouchLCD43c() {
        InitializePowerSaveTimer();
        InitializeGpio();
        InitializeCodecI2c();
        InitializeCustomio();
        InitializeSdCard();
        InitializeRGB();
        InitializeTouch();
        InitializeTools();
        GetBacklight()->SetBrightness(100);

        // Schedule random emoji playback after display is ready
        if (is_sdcard_found_) {
            xTaskCreate([](void* arg) {
                auto* board = static_cast<WaveshareEsp32s3TouchLCD43c*>(arg);
                // Wait for display to fully initialize
                vTaskDelay(pdMS_TO_TICKS(3000));
                board->PlayRandomEmojiFromSdCard();
                vTaskDelete(NULL);
            }, "sd_emoji_task", 4096, this, tskIDLE_PRIORITY + 1, NULL);
        }
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            BSP_I2S_MCLK, 
            BSP_I2S_SCLK, 
            BSP_I2S_LCLK, 
            BSP_I2S_DOUT, 
            BSP_I2S_DSIN,
            BSP_PA_PIN, 
            BSP_CODEC_ES8311_ADDR, 
            BSP_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight *GetBacklight() override {
         return backlight_;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }

    virtual void* GetI2cBus() override {
        return (void*)i2c_bus_;
    }
};

DECLARE_BOARD(WaveshareEsp32s3TouchLCD43c);
