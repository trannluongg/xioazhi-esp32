#include "emoji_collection.h"

#include <esp_log.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <deque>
#include <string>
#include <unordered_map>
#include <dirent.h>
#include <vector>
#include "lvgl_image.h"


#define TAG "EmojiCollection"

void EmojiCollection::AddEmoji(const std::string& name, LvglImage* image) {
    emoji_collection_[name] = image;
}

const LvglImage* EmojiCollection::GetEmojiImage(const char* name) {
    // Check if we already have a file-based override in the collection
    auto it = emoji_collection_.find(name);
    
    // Check SD card for directory-based random emojis first
    std::string dir_path = "/sdcard/emoji/default/" + std::string(name);
    struct stat st;
    if (stat(dir_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(dir_path.c_str());
        if (dir) {
            std::vector<std::string> files;
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                std::string file_name = entry->d_name;
                if (file_name.find(".gif") != std::string::npos || file_name.find(".GIF") != std::string::npos) {
                    files.push_back(file_name);
                }
            }
            closedir(dir);

            if (!files.empty()) {
                int index = rand() % files.size();
                std::string lv_path = "S:/emoji/default/" + std::string(name) + "/" + files[index];
                ESP_LOGI(TAG, "Using random SD card emoji from dir: %s", lv_path.c_str());

                auto image = new LvglFileImage(lv_path);
                if (it != emoji_collection_.end()) {
                    delete it->second;
                }
                emoji_collection_[name] = image;
                // Note: LRU not strictly updated here as we always create a new one, 
                // but we might want to still add to LRU for memory management if we had shared images.
                // For now, replacing the entry is enough as dynamic_cast check will handle it.
                return image;
            }
        }
    }

    if (it != emoji_collection_.end()) {
        if (dynamic_cast<const LvglFileImage*>(it->second)) {
            // Update LRU: move to back
            auto lru_it = std::find(sd_emoji_lru_.begin(), sd_emoji_lru_.end(), name);
            if (lru_it != sd_emoji_lru_.end()) {
                sd_emoji_lru_.erase(lru_it);
            }
            sd_emoji_lru_.push_back(name);
            return it->second;
        }
    }

    // Check SD card for overrides using real path (backward compatibility)
    std::string base_path = "/sdcard/emoji/" + std::string(name);
    std::string found_ext;

    if (access((base_path + ".gif").c_str(), F_OK) == 0) {
        found_ext = ".gif";
    } else if (access((base_path + ".GIF").c_str(), F_OK) == 0) {
        found_ext = ".GIF";
    }

    if (!found_ext.empty()) {
        std::string lv_path = "S:/emoji/" + std::string(name) + found_ext;
        ESP_LOGI(TAG, "Using SD card emoji: %s", lv_path.c_str());

        auto image = new LvglFileImage(lv_path);

        // Update LRU and handle eviction
        sd_emoji_lru_.push_back(name);
        if (sd_emoji_lru_.size() > max_sd_emojis_) {
            std::string to_evict = sd_emoji_lru_.front();
            sd_emoji_lru_.pop_front();

            auto evict_it = emoji_collection_.find(to_evict);
            if (evict_it != emoji_collection_.end()) {
                ESP_LOGI(TAG, "Evicting oldest SD emoji from PSRAM: %s", to_evict.c_str());
                delete evict_it->second;
                emoji_collection_.erase(evict_it);

                // If the one we just created was to_evict (shouldn't happen with max=2, but just in case)
                if (to_evict == name) {
                    it = emoji_collection_.end();
                } else {
                    it = emoji_collection_.find(name);
                }
            }
        }

        // If we had a source image (built-in), delete it and replace with SD version
        if (it != emoji_collection_.end()) {
            delete it->second;
        }
        emoji_collection_[name] = image;
        return image;
    }

    // Fallback to built-in image if exists in collection
    if (it != emoji_collection_.end()) {
        return it->second;
    }

    ESP_LOGW(TAG, "Emoji not found in collection or SD card: %s", name);
    return nullptr;
}

EmojiCollection::~EmojiCollection() {
    for (auto it = emoji_collection_.begin(); it != emoji_collection_.end(); ++it) {
        delete it->second;
    }
    emoji_collection_.clear();
}

// These are declared in xiaozhi-fonts/src/font_emoji_32.c
extern const lv_image_dsc_t emoji_1f636_32;  // neutral
extern const lv_image_dsc_t emoji_1f642_32;  // happy
extern const lv_image_dsc_t emoji_1f606_32;  // laughing
extern const lv_image_dsc_t emoji_1f602_32;  // funny
extern const lv_image_dsc_t emoji_1f614_32;  // sad
extern const lv_image_dsc_t emoji_1f620_32;  // angry
extern const lv_image_dsc_t emoji_1f62d_32;  // crying
extern const lv_image_dsc_t emoji_1f60d_32;  // loving
extern const lv_image_dsc_t emoji_1f633_32;  // embarrassed
extern const lv_image_dsc_t emoji_1f62f_32;  // surprised
extern const lv_image_dsc_t emoji_1f631_32;  // shocked
extern const lv_image_dsc_t emoji_1f914_32;  // thinking
extern const lv_image_dsc_t emoji_1f609_32;  // winking
extern const lv_image_dsc_t emoji_1f60e_32;  // cool
extern const lv_image_dsc_t emoji_1f60c_32;  // relaxed
extern const lv_image_dsc_t emoji_1f924_32;  // delicious
extern const lv_image_dsc_t emoji_1f618_32;  // kissy
extern const lv_image_dsc_t emoji_1f60f_32;  // confident
extern const lv_image_dsc_t emoji_1f634_32;  // sleepy
extern const lv_image_dsc_t emoji_1f61c_32;  // silly
extern const lv_image_dsc_t emoji_1f644_32;  // confused

Twemoji32::Twemoji32() {
    AddEmoji("neutral", new LvglSourceImage(&emoji_1f636_32));
    AddEmoji("happy", new LvglSourceImage(&emoji_1f642_32));
    AddEmoji("laughing", new LvglSourceImage(&emoji_1f606_32));
    AddEmoji("funny", new LvglSourceImage(&emoji_1f602_32));
    AddEmoji("sad", new LvglSourceImage(&emoji_1f614_32));
    AddEmoji("angry", new LvglSourceImage(&emoji_1f620_32));
    AddEmoji("crying", new LvglSourceImage(&emoji_1f62d_32));
    AddEmoji("loving", new LvglSourceImage(&emoji_1f60d_32));
    AddEmoji("embarrassed", new LvglSourceImage(&emoji_1f633_32));
    AddEmoji("surprised", new LvglSourceImage(&emoji_1f62f_32));
    AddEmoji("shocked", new LvglSourceImage(&emoji_1f631_32));
    AddEmoji("thinking", new LvglSourceImage(&emoji_1f914_32));
    AddEmoji("winking", new LvglSourceImage(&emoji_1f609_32));
    AddEmoji("cool", new LvglSourceImage(&emoji_1f60e_32));
    AddEmoji("relaxed", new LvglSourceImage(&emoji_1f60c_32));
    AddEmoji("delicious", new LvglSourceImage(&emoji_1f924_32));
    AddEmoji("kissy", new LvglSourceImage(&emoji_1f618_32));
    AddEmoji("confident", new LvglSourceImage(&emoji_1f60f_32));
    AddEmoji("sleepy", new LvglSourceImage(&emoji_1f634_32));
    AddEmoji("silly", new LvglSourceImage(&emoji_1f61c_32));
    AddEmoji("confused", new LvglSourceImage(&emoji_1f644_32));
}

// These are declared in xiaozhi-fonts/src/font_emoji_64.c
extern const lv_image_dsc_t emoji_1f636_64;  // neutral
extern const lv_image_dsc_t emoji_1f642_64;  // happy
extern const lv_image_dsc_t emoji_1f606_64;  // laughing
extern const lv_image_dsc_t emoji_1f602_64;  // funny
extern const lv_image_dsc_t emoji_1f614_64;  // sad
extern const lv_image_dsc_t emoji_1f620_64;  // angry
extern const lv_image_dsc_t emoji_1f62d_64;  // crying
extern const lv_image_dsc_t emoji_1f60d_64;  // loving
extern const lv_image_dsc_t emoji_1f633_64;  // embarrassed
extern const lv_image_dsc_t emoji_1f62f_64;  // surprised
extern const lv_image_dsc_t emoji_1f631_64;  // shocked
extern const lv_image_dsc_t emoji_1f914_64;  // thinking
extern const lv_image_dsc_t emoji_1f609_64;  // winking
extern const lv_image_dsc_t emoji_1f60e_64;  // cool
extern const lv_image_dsc_t emoji_1f60c_64;  // relaxed
extern const lv_image_dsc_t emoji_1f924_64;  // delicious
extern const lv_image_dsc_t emoji_1f618_64;  // kissy
extern const lv_image_dsc_t emoji_1f60f_64;  // confident
extern const lv_image_dsc_t emoji_1f634_64;  // sleepy
extern const lv_image_dsc_t emoji_1f61c_64;  // silly
extern const lv_image_dsc_t emoji_1f644_64;  // confused

Twemoji64::Twemoji64() {
    AddEmoji("neutral", new LvglSourceImage(&emoji_1f636_64));
    AddEmoji("happy", new LvglSourceImage(&emoji_1f642_64));
    AddEmoji("laughing", new LvglSourceImage(&emoji_1f606_64));
    AddEmoji("funny", new LvglSourceImage(&emoji_1f602_64));
    AddEmoji("sad", new LvglSourceImage(&emoji_1f614_64));
    AddEmoji("angry", new LvglSourceImage(&emoji_1f620_64));
    AddEmoji("crying", new LvglSourceImage(&emoji_1f62d_64));
    AddEmoji("loving", new LvglSourceImage(&emoji_1f60d_64));
    AddEmoji("embarrassed", new LvglSourceImage(&emoji_1f633_64));
    AddEmoji("surprised", new LvglSourceImage(&emoji_1f62f_64));
    AddEmoji("shocked", new LvglSourceImage(&emoji_1f631_64));
    AddEmoji("thinking", new LvglSourceImage(&emoji_1f914_64));
    AddEmoji("winking", new LvglSourceImage(&emoji_1f609_64));
    AddEmoji("cool", new LvglSourceImage(&emoji_1f60e_64));
    AddEmoji("relaxed", new LvglSourceImage(&emoji_1f60c_64));
    AddEmoji("delicious", new LvglSourceImage(&emoji_1f924_64));
    AddEmoji("kissy", new LvglSourceImage(&emoji_1f618_64));
    AddEmoji("confident", new LvglSourceImage(&emoji_1f60f_64));
    AddEmoji("sleepy", new LvglSourceImage(&emoji_1f634_64));
    AddEmoji("silly", new LvglSourceImage(&emoji_1f61c_64));
    AddEmoji("confused", new LvglSourceImage(&emoji_1f644_64));
}
