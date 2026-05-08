#include "application.h"
#include "audio/audio_codec.h"
#include "audio/codecs/box_audio_codec.h"
#include "boards/common/camera.h"
#include "boards/common/wifi_board.h"
#include "display/display.h"
#include "display/lcd_display.h"

#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include "button.h"
#include "driver/sdmmc_host.h"
#include "esp_cam_sensor_xclk.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_random.h"
#include "esp_vfs_fat.h"
#include "esp_video.h"
#include "esp_video_init.h"
#include "sdmmc_cmd.h"

#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4B
#include "esp_lcd_st7703.h"
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_7B
#include "esp_lcd_ek79007.h"
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_3_4C || \
    CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4C ||     \
    CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_8 ||      \
    CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_10_1
#include "esp_lcd_jd9365.h"
// Use ST7701 LCD driver for 4.3 inch (same as 4_3C)
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || \
    CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3C || \
    CONFIG_BOARD_TYPE_DODOMIO_P4_4_3C
#include "esp_lcd_st7701.h"
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_7
#include "esp_lcd_ili9881c.h"
#endif

#include "assets/lang_config.h"
#include "config.h"
#include "lcd_init_cmds.h"

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include "esp_lcd_touch_gt911.h"

#define TAG "DodoMio"

class DodomioP4_4_3C : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    Button touch_sensor_;
    Button touch_sensor_21_;
    Button touch_sensor_25_;
    Button touch_sensor_24_;
    Button volume_up_button_;
    Button volume_down_button_;
    Button speak_button_;
    LcdDisplay* display_;
    EspVideo* camera_ = nullptr;
    esp_timer_handle_t touch_timer_ = nullptr;

    esp_err_t i2c_device_probe(uint8_t addr) { return i2c_master_probe(i2c_bus_, addr, 10); }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                    .enable_internal_pullup = 1,
                },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

        // NOTE: I2C scan removed from boot sequence.
        // Use I2cScan() MCP tool for on-demand scanning if needed.
        // Scanning during boot causes I2C bus lockup when STM32 slave is connected.
    }

    static esp_err_t bsp_enable_dsi_phy_power(void) {
#if MIPI_DSI_PHY_PWR_LDO_CHAN > 0
        // Turn on the power for MIPI DSI PHY, so it can go from "No Power" state to "Shutdown"
        // state
        static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
            .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
        };
        esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan);
        ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif  // BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0

        return ESP_OK;
    }

    void InitializeLCD() {
        bsp_enable_dsi_phy_power();
        vTaskDelay(pdMS_TO_TICKS(200)); // Wait for power to stabilize
        
        // Force backlight on
        gpio_config_t bk_cfg = {
            .pin_bit_mask = 1ULL << DISPLAY_BACKLIGHT_PIN,
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&bk_cfg);
        gpio_set_level(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT ? 0 : 1);

        esp_lcd_panel_io_handle_t io = NULL;
        esp_lcd_panel_handle_t disp_panel = NULL;

        esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
        esp_lcd_dsi_bus_config_t bus_config = {
            .bus_id = 0,
            .num_data_lanes = 2,
            .lane_bit_rate_mbps = LCD_MIPI_DSI_LANE_BITRATE_MBPS,
        };
        ESP_LOGI(TAG, "New MIPI DSI bus");
        ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

        ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
        // we use DBI interface to send LCD commands and parameters
        esp_lcd_dbi_io_config_t dbi_config = {
            .virtual_channel = 0,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
        };
        ESP_LOGI(TAG, "New MIPI DSI DBI IO");
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io));
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4B
        esp_lcd_dpi_panel_config_t dpi_config = {
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 46,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing =
                {
                    .h_size = 720,
                    .v_size = 720,
                    .hsync_pulse_width = 20,
                    .hsync_back_porch = 80,
                    .hsync_front_porch = 80,
                    .vsync_pulse_width = 4,
                    .vsync_back_porch = 12,
                    .vsync_front_porch = 30,
                },
            .flags =
                {
                    .use_dma2d = true,
                },
        };
        st7703_vendor_config_t vendor_config = {

            .mipi_config =
                {
                    .dsi_bus = mipi_dsi_bus,
                    .dpi_config = &dpi_config,
                },
            .flags =
                {
                    .use_mipi_interface = 1,
                },
        };

        const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        esp_lcd_new_panel_st7703(io, &lcd_dev_config, &disp_panel);
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_7B
        esp_lcd_dpi_panel_config_t dpi_config = {
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 52,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing =
                {
                    .h_size = 1024,
                    .v_size = 600,
                    .hsync_pulse_width = 10,
                    .hsync_back_porch = 160,
                    .hsync_front_porch = 160,
                    .vsync_pulse_width = 1,
                    .vsync_back_porch = 23,
                    .vsync_front_porch = 12,
                },
            .flags =
                {
                    .use_dma2d = true,
                },
        };
        ek79007_vendor_config_t vendor_config = {
            .mipi_config =
                {
                    .dsi_bus = mipi_dsi_bus,
                    .dpi_config = &dpi_config,
                },
        };

        const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        esp_lcd_new_panel_ek79007(io, &lcd_dev_config, &disp_panel);
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_3_4C || \
    CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4C
        esp_lcd_dpi_panel_config_t dpi_config = {
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 46,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing =
                {
                    .h_size = DISPLAY_WIDTH,
                    .v_size = DISPLAY_HEIGHT,
                    .hsync_pulse_width = 20,
                    .hsync_back_porch = 20,
                    .hsync_front_porch = 40,
                    .vsync_pulse_width = 4,
                    .vsync_back_porch = 12,
                    .vsync_front_porch = 24,
                },
            .flags =
                {
                    .use_dma2d = true,
                },
        };
        jd9365_vendor_config_t vendor_config = {
            .init_cmds = lcd_init_cmds,
            .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
            .mipi_config =
                {
                    .dsi_bus = mipi_dsi_bus,
                    .dpi_config = &dpi_config,
                    .lane_num = 2,
                },
        };

        const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_jd9365(io, &lcd_dev_config, &disp_panel));
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || \
    CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3C || \
    CONFIG_BOARD_TYPE_DODOMIO_P4_4_3C
        ESP_LOGI(TAG, "Initialize ST7701 panel");
        esp_lcd_dpi_panel_config_t dpi_config = {
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = LCD_DPI_CLOCK_FREQ_MHZ,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing =
                {
                    .h_size = LCD_H_RES,
                    .v_size = LCD_V_RES,
                    .hsync_pulse_width = LCD_HSYNC_PULSE_WIDTH,
                    .hsync_back_porch = LCD_HSYNC_BACK_PORCH,
                    .hsync_front_porch = LCD_HSYNC_FRONT_PORCH,
                    .vsync_pulse_width = LCD_VSYNC_PULSE_WIDTH,
                    .vsync_back_porch = LCD_VSYNC_BACK_PORCH,
                    .vsync_front_porch = LCD_VSYNC_FRONT_PORCH,
                },
            .flags =
                {
                    .use_dma2d = true,
                },
        };
        st7701_vendor_config_t vendor_config = {
            .init_cmds = lcd_init_cmds,
            .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
            .mipi_config =
                {
                    .dsi_bus = mipi_dsi_bus,
                    .dpi_config = &dpi_config,
                },
            .flags =
                {
                    .use_mipi_interface = 1,
                },
        };

        const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(io, &lcd_dev_config, &disp_panel));
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_8 || \
    CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_10_1
        esp_lcd_dpi_panel_config_t dpi_config = {
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 52,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing =
                {
                    .h_size = DISPLAY_WIDTH,
                    .v_size = DISPLAY_HEIGHT,
                    .hsync_pulse_width = 20,
                    .hsync_back_porch = 20,
                    .hsync_front_porch = 40,
                    .vsync_pulse_width = 4,
                    .vsync_back_porch = 10,
                    .vsync_front_porch = 30,
                },
            .flags =
                {
                    .use_dma2d = true,
                },
        };
        jd9365_vendor_config_t vendor_config = {
            .init_cmds = lcd_init_cmds,
            .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
            .mipi_config =
                {
                    .dsi_bus = mipi_dsi_bus,
                    .dpi_config = &dpi_config,
                    .lane_num = 2,
                },
        };

        const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        esp_lcd_new_panel_jd9365(io, &lcd_dev_config, &disp_panel);
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_7
        esp_lcd_dpi_panel_config_t dpi_config = {
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 80,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing =
                {
                    .h_size = DISPLAY_WIDTH,
                    .v_size = DISPLAY_HEIGHT,
                    .hsync_pulse_width = 50,
                    .hsync_back_porch = 239,
                    .hsync_front_porch = 33,
                    .vsync_pulse_width = 30,
                    .vsync_back_porch = 20,
                    .vsync_front_porch = 2,
                },
            .flags =
                {
                    .use_dma2d = true,
                },
        };
        ili9881c_vendor_config_t vendor_config = {
            .init_cmds = lcd_init_cmds,
            .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
            .mipi_config =
                {
                    .dsi_bus = mipi_dsi_bus,
                    .dpi_config = &dpi_config,
                    .lane_num = 2,
                },
        };

        const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        esp_lcd_new_panel_ili9881c(io, &lcd_dev_config, &disp_panel);
#endif
        ESP_LOGI(TAG, "Reset LCD panel");
        gpio_config_t rst_cfg = {
            .pin_bit_mask = 1ULL << PIN_NUM_LCD_RST,
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&rst_cfg);
        gpio_set_level(PIN_NUM_LCD_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(PIN_NUM_LCD_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(120));

        ESP_LOGI(TAG, "Init LCD panel");
        ESP_ERROR_CHECK(esp_lcd_panel_init(disp_panel));
        ESP_LOGI(TAG, "LCD panel initialized. Creating MipiLcdDisplay with %dx%d", DISPLAY_WIDTH, DISPLAY_HEIGHT);
        display_ = new MipiLcdDisplay(io, disp_panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                      DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                      DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }
    void InitializeTouch() {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_SWAP_XY ? DISPLAY_HEIGHT : DISPLAY_WIDTH,
            .y_max = DISPLAY_SWAP_XY ? DISPLAY_WIDTH : DISPLAY_HEIGHT,
            .rst_gpio_num = GPIO_NUM_23,
            .int_gpio_num = GPIO_NUM_NC,
            .levels =
                {
                    .reset = 0,
                    .interrupt = 0,
                },
            .flags =
                {
                    .swap_xy = DISPLAY_SWAP_XY,
                    .mirror_x = DISPLAY_MIRROR_X,
                    .mirror_y = DISPLAY_MIRROR_Y,
                },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = {};
        tp_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
        tp_io_config.control_phase_bytes = 1;
        tp_io_config.lcd_cmd_bits = 8;
        tp_io_config.lcd_param_bits = 8;
        if (ESP_OK == i2c_device_probe(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS)) {
            ESP_LOGI(TAG, "Touch panel found at address 0x%02X",
                     ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS);
        } else if (ESP_OK == i2c_device_probe(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP)) {
            ESP_LOGI(TAG, "Touch panel found at address 0x%02X",
                     ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP);
            tp_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
        } else {
            ESP_LOGE(TAG, "Touch panel not found on I2C bus");
            ESP_LOGE(TAG, "Tried addresses: 0x%02X and 0x%02X", ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
                     ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP);
            return;
        }

        tp_io_config.scl_speed_hz = 400 * 1000;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle));
        ESP_LOGI(TAG, "Initialize touch controller");
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp));
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = tp,
        };
        lvgl_port_add_touch(&touch_cfg);
        ESP_LOGI(TAG, "Touch panel initialized successfully");
    }
    void InitializeCamera() {
        esp_video_init_csi_config_t base_csi_config = {
            .sccb_config =
                {
                    .init_sccb = false,
                    .i2c_handle = i2c_bus_,
                    .freq = 400000,
                },
            .reset_pin = GPIO_NUM_NC,
            .pwdn_pin = GPIO_NUM_NC,
        };

        esp_video_init_config_t cam_config = {
            .csi = &base_csi_config,
        };

        camera_ = new EspVideo(cam_config);
    }
    void ListDirectory(const char* path, int level = 0) {
        DIR* dir = opendir(path);
        if (!dir) {
            ESP_LOGE(TAG, "Failed to open directory %s", path);
            return;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            char indent[32] = {0};
            for (int i = 0; i < level && i < 30; i++)
                indent[i] = ' ';

            if (entry->d_type == DT_DIR) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                    continue;
                ESP_LOGI(TAG, "%s[%s]", indent, entry->d_name);
                std::string next_path = std::string(path) + "/" + entry->d_name;
                ListDirectory(next_path.c_str(), level + 2);
            } else {
                ESP_LOGI(TAG, "%s%s", indent, entry->d_name);
            }
        }
        closedir(dir);
    }

    void InitializeSdCard() {
        ESP_LOGI(TAG, "Initializing SD card power (GPIO45)...");
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << SD_POWER_PIN);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);
        gpio_set_level((gpio_num_t)SD_POWER_PIN, 0);  // Active LOW to turn on AO3401 MOSFET

        // Enable VO4 LDO for SD card pull-ups (as seen in schematic)
        static esp_ldo_channel_handle_t sd_ldo_chan = NULL;
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = 4,  // VO4
            .voltage_mv = 3300,
        };
        if (esp_ldo_acquire_channel(&ldo_cfg, &sd_ldo_chan) == ESP_OK) {
            ESP_LOGI(TAG, "SD LDO VO4 (3.3V) enabled");
        }

        vTaskDelay(pdMS_TO_TICKS(100));  // Wait for power to stabilize

        ESP_LOGI(TAG, "Mounting SD card...");
        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false, .max_files = 5, .allocation_unit_size = 16 * 1024};
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.slot = SDMMC_HOST_SLOT_0;
        host.max_freq_khz = 40000;  // Increased to 40MHz for better performance

        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.width = 4;
        slot_config.clk = SDMMC_CLK;
        slot_config.cmd = SDMMC_CMD;
        slot_config.d0 = SDMMC_D0;
        slot_config.d1 = SDMMC_D1;
        slot_config.d2 = SDMMC_D2;
        slot_config.d3 = SDMMC_D3;
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        sdmmc_card_t* card;
        esp_err_t ret =
            esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

        if (ret != ESP_OK) {
            if (ret == ESP_FAIL) {
                ESP_LOGE(
                    TAG,
                    "Failed to mount filesystem. "
                    "If you want the card to be formatted, set format_if_mount_failed = true.");
            } else {
                ESP_LOGE(TAG,
                         "Failed to initialize the card (%s). "
                         "Make sure SD card lines have pull-up resistors in place.",
                         esp_err_to_name(ret));
            }
            return;
        }
        ESP_LOGI(TAG, "Filesystem mounted at /sdcard");
        sdmmc_card_print_info(stdout, card);

        // Check if /sdcard/dodomio/emoji exists
        struct stat st;
        if (stat("/sdcard/dodomio/emoji", &st) == 0 && S_ISDIR(st.st_mode)) {
            ESP_LOGI(TAG, "Found /sdcard/dodomio/emoji directory. Listing files:");
            ListDirectory("/sdcard/dodomio/emoji");
        } else {
            ESP_LOGW(TAG, "/sdcard/dodomio/emoji directory NOT found! Listing root directory instead:");
            ListDirectory("/sdcard");
        }
    }
    void InitializeTouchSensor() {
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) { Application::GetInstance().DismissAlert(); },
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "touch_timer"};
        esp_timer_create(&timer_args, &touch_timer_);

        touch_sensor_.OnPressDown([this]() {
            Application::GetInstance().Alert("Info", "DODO nhột quá", "happy",
                                             Lang::Sounds::OGG_POPUP);
            esp_timer_stop(touch_timer_);
            esp_timer_start_once(touch_timer_, 2000000);
        });

        auto random_emoji_callback = [this]() {
            const char* emojis[] = {"kissy", "loving"};
            const char* selected = emojis[esp_random() % 2];
            Application::GetInstance().Alert("Info", "Yêu quá đi!", selected,
                                             Lang::Sounds::OGG_POPUP);
            esp_timer_stop(touch_timer_);
            esp_timer_start_once(touch_timer_, 2000000);
        };

        touch_sensor_21_.OnPressDown(random_emoji_callback);
        touch_sensor_25_.OnPressDown(random_emoji_callback);
        touch_sensor_24_.OnPressDown(random_emoji_callback);
    }
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            // During startup (before connected), pressing BOOT button enters Wi-Fi config mode
            // without reboot
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            auto codec = GetAudioCodec();
            codec->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(100));
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            auto codec = GetAudioCodec();
            codec->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(0));
        });

        speak_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting)
                return;
            // Dùng ToggleChatState để bắt đầu nghe với chế độ tự động ngắt (AutoStop)
            // Đây là chế độ tương tự như khi gọi wake word, nhận diện giọng nói ổn định hơn.
            app.ToggleChatState();
        });
    }

public:
    DodomioP4_4_3C()
        : boot_button_(BOOT_BUTTON_GPIO),
          touch_sensor_(TOUCH_SENSOR_GPIO, true),
          touch_sensor_21_(GPIO_NUM_21, false),
          touch_sensor_25_(GPIO_NUM_25, false),
          touch_sensor_24_(GPIO_NUM_24, false),
          volume_up_button_(GPIO_NUM_31),
          volume_down_button_(GPIO_NUM_29),
          speak_button_(GPIO_NUM_30) {
        InitializeCodecI2c();
        InitializeLCD();
        InitializeTouch();
        InitializeCamera();
        InitializeButtons();
        InitializeTouchSensor();
        InitializeSdCard();
        GetBacklight()->RestoreBrightness();
        // NOTE: I2cScan() removed from boot — causes WDT crash when STM32 slave is connected.
        // Use MCP tool self.stm32.send_command for on-demand I2C scanning.
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR, AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        return (AudioCodec*)&audio_codec;
    }

    virtual Display* GetDisplay() override { return (Display*)display_; }

    virtual Camera* GetCamera() override { return (Camera*)camera_; }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual void I2cScan() override {
        printf("Scanning I2C bus...\n");
        uint8_t count = 0;
        for (uint8_t addr = 8; addr < 120; addr++) {  // skip reserved addresses
            if (i2c_device_probe(addr) == ESP_OK) {
                printf(" - Found I2C device at address 0x%02X\n", addr);
                count++;
            }
            vTaskDelay(1);  // feed watchdog between probes
        }
        if (count == 0) {
            printf(" - No I2C devices found\n");
        } else {
            printf(" - Found %d I2C devices\n", count);
        }
    }

    virtual bool I2cWrite(uint8_t addr, uint8_t reg, uint8_t value) override {
        uint8_t data[] = {reg, value};
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000,
        };
        i2c_master_dev_handle_t dev_handle;
        if (i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &dev_handle) != ESP_OK) {
            return false;
        }
        esp_err_t err = i2c_master_transmit(dev_handle, data, sizeof(data), -1);
        i2c_master_bus_rm_device(dev_handle);
        return err == ESP_OK;
    }
};

DECLARE_BOARD(DodomioP4_4_3C);
