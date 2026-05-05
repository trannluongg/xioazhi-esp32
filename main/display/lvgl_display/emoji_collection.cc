#include "emoji_collection.h"

#include <esp_log.h>
#include <esp_vfs.h>
#include <esp_vfs_fat.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>

static const char *TAG = "EmojiCollection";

void EmojiCollection::AddEmoji(const std::string& name, LvglImage* image) {
    emoji_collection_[name] = image;
}

const LvglImage* EmojiCollection::GetEmojiImage(const char* name) {
    auto it = emoji_collection_.find(name);
    if (it != emoji_collection_.end()) {
        return it->second;
    }

    ESP_LOGW(TAG, "Emoji not found: %s", name);
    return nullptr;
}

const LvglImage* EmojiCollection::GetRandomVariant(const char* name) {
    // Server luôn gửi exact name (happy_01, happy_02, etc.)
    // Nên chỉ cần load exact name, không random
    
    // Try to load on-demand if not found
    if (emoji_collection_.find(name) == emoji_collection_.end()) {
        ủa(name);
    }
    
    auto image = GetEmojiImage(name);
    if (image == nullptr) {
        ESP_LOGW(TAG, "Emoji not found: %s", name);
    }
    return image;
}

EmojiCollection::~EmojiCollection() {
    for (auto it = emoji_collection_.begin(); it != emoji_collection_.end(); ++it) {
        delete it->second;
    }
    emoji_collection_.clear();
}

void EmojiCollection::LoadFromSD(const char* base_path) {
    static const char* TAG = "LoadEmojiSD";
    
    ESP_LOGI(TAG, "Loading emojis from SD: %s", base_path);
    
    // Clear any existing emojis - use ONLY SD emojis
    emoji_collection_.clear();
    
    // Open directory
    DIR* dir = opendir(base_path);
    if (dir == nullptr) {
        ESP_LOGW(TAG, "Cannot open emoji directory: %s", base_path);
        return;
    }
    
    struct dirent* entry;
    int loaded_count = 0;
    const int MAX_EMOJIS = 1;  // Only load default at startup, others on-demand
    
    // Scan all files in directory
    while ((entry = readdir(dir)) != nullptr) {
        // Skip . and ..
        if (entry->d_name[0] == '.') continue;
        
        // Check for .gif extension
        const char* ext = strrchr(entry->d_name, '.');
        if (ext == nullptr || strcasecmp(ext, ".gif") != 0) {
            continue;
        }
        
        // Build full path
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", base_path, entry->d_name);
        
        // Get file size
        struct stat st;
        if (stat(filepath, &st) != 0) {
            ESP_LOGW(TAG, "Cannot stat file: %s", filepath);
            continue;
        }
        
        // Open file
        FILE* f = fopen(filepath, "rb");
        if (f == nullptr) {
            ESP_LOGW(TAG, "Cannot open file: %s", filepath);
            continue;
        }
        
        // Allocate buffer and read file
        void* data = malloc(st.st_size);
        if (data == nullptr) {
            ESP_LOGW(TAG, "Cannot allocate memory for: %s", entry->d_name);
            fclose(f);
            continue;
        }
        
        size_t bytes_read = fread(data, 1, st.st_size, f);
        fclose(f);
        
        if (bytes_read != (size_t)st.st_size) {
            ESP_LOGW(TAG, "Incomplete read from: %s", filepath);
            free(data);
            continue;
        }
        
        // Extract name without extension: happy_01.gif → "happy_01"
        std::string name(entry->d_name, ext - entry->d_name);
        
        // Convert to lowercase for consistency: "RELAXE~1" → "relaxed_01"
        for (char& c : name) {
            if (c >= 'A' && c <= 'Z') c = c + 32;
            if (c == '~') c = '_';  // Fix ~ to _
        }
        
        // Create image and add to collection with exact name
        LvglRawImage* image = new LvglRawImage(data, st.st_size);
        AddEmoji(name, image);
        
        loaded_count++;
        if (loaded_count >= MAX_EMOJIS) {
            ESP_LOGW(TAG, "Reached max emojis limit (%d), stopping load", MAX_EMOJIS);
            // Drain remaining directory entries before closing
            while (readdir(dir) != nullptr) { }
            break;
        }
        ESP_LOGI(TAG, "Loaded emoji: %s (%d bytes)", name.c_str(), (int)st.st_size);
    }
    
    // If default.gif exists in directory but wasn't loaded, load it
    if (loaded_count > 0 && loaded_count < MAX_EMOJIS) {
        char default_path[512];
        snprintf(default_path, sizeof(default_path), "%s/default.gif", base_path);
        struct stat st_default;
        if (stat(default_path, &st_default) == 0) {
            // Load default.gif
            FILE* f = fopen(default_path, "rb");
            if (f) {
                void* data = malloc(st_default.st_size);
                if (data) {
                    size_t bytes_read = fread(data, 1, st_default.st_size, f);
                    fclose(f);
                    if (bytes_read == (size_t)st_default.st_size) {
                        emoji_collection_["default"] = new LvglRawImage(data, st_default.st_size);
                        ESP_LOGI(TAG, "Added 'default' alias from file");
                        loaded_count++;
                    } else {
                        free(data);
                    }
                } else {
                    fclose(f);
                }
            }
        }
    }
    
    closedir(dir);
    
    // Also add alias if not already added
    if (loaded_count > 0) {
        // Find first loaded emoji and add alias
        for (auto& pair : emoji_collection_) {
            if (pair.first != "default") {
                emoji_collection_["default"] = pair.second;
                ESP_LOGI(TAG, "Added 'default' alias to: %s", pair.first.c_str());
            }
            break;
        }
    }
    
    ESP_LOGI(TAG, "Loaded %d emojis from SD", loaded_count);
}

bool EmojiCollection::LoadEmoji(const char* name, const char* base_path) {
    // Check if already loaded
    if (emoji_collection_.find(name) != emoji_collection_.end()) {
        ESP_LOGI(TAG, "Emoji already loaded: %s", name);
        return true;
    }
    
    // Build filename: name + .gif
    std::string filename = name;
    filename += ".gif";
    
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", base_path, filename.c_str());
    
    ESP_LOGI(TAG, "LoadEmoji: looking for %s", filepath);
    
    // Check file exists
    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGW(TAG, "Emoji file not found: %s", filepath);
        return false;
    }
    
    // Open and load file
    FILE* f = fopen(filepath, "rb");
    if (f == nullptr) {
        ESP_LOGW(TAG, "Cannot open emoji file: %s", filepath);
        return false;
    }
    
    void* data = malloc(st.st_size);
    if (data == nullptr) {
        ESP_LOGW(TAG, "Cannot allocate memory for: %s", name);
        fclose(f);
        return false;
    }
    
    size_t bytes_read = fread(data, 1, st.st_size, f);
    fclose(f);
    
    if (bytes_read != (size_t)st.st_size) {
        ESP_LOGW(TAG, "Incomplete read from: %s", filepath);
        free(data);
        return false;
    }
    
    // Add to collection
    std::string key(name);
    // Normalize: uppercase→lowercase, ~→_
    for (char& c : key) {
        if (c >= 'A' && c <= 'Z') c = c + 32;
        if (c == '~') c = '_';
    }
    
    LvglRawImage* image = new LvglRawImage(data, st.st_size);
    emoji_collection_[key] = image;
    
    ESP_LOGI(TAG, "Loaded emoji on-demand: %s (%d bytes)", key.c_str(), (int)st.st_size);
    return true;
}

// These are declared in xiaozhi-fonts/src/font_emoji_32.c
extern const lv_image_dsc_t emoji_1f636_32; // neutral
extern const lv_image_dsc_t emoji_1f642_32; // happy
extern const lv_image_dsc_t emoji_1f606_32; // laughing
extern const lv_image_dsc_t emoji_1f602_32; // funny
extern const lv_image_dsc_t emoji_1f614_32; // sad
extern const lv_image_dsc_t emoji_1f620_32; // angry
extern const lv_image_dsc_t emoji_1f62d_32; // crying
extern const lv_image_dsc_t emoji_1f60d_32; // loving
extern const lv_image_dsc_t emoji_1f633_32; // embarrassed
extern const lv_image_dsc_t emoji_1f62f_32; // surprised
extern const lv_image_dsc_t emoji_1f631_32; // shocked
extern const lv_image_dsc_t emoji_1f914_32; // thinking
extern const lv_image_dsc_t emoji_1f609_32; // winking
extern const lv_image_dsc_t emoji_1f60e_32; // cool
extern const lv_image_dsc_t emoji_1f60c_32; // relaxed
extern const lv_image_dsc_t emoji_1f924_32; // delicious
extern const lv_image_dsc_t emoji_1f618_32; // kissy
extern const lv_image_dsc_t emoji_1f60f_32; // confident
extern const lv_image_dsc_t emoji_1f634_32; // sleepy
extern const lv_image_dsc_t emoji_1f61c_32; // silly
extern const lv_image_dsc_t emoji_1f644_32; // confused

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
extern const lv_image_dsc_t emoji_1f636_64; // neutral
extern const lv_image_dsc_t emoji_1f642_64; // happy
extern const lv_image_dsc_t emoji_1f606_64; // laughing
extern const lv_image_dsc_t emoji_1f602_64; // funny
extern const lv_image_dsc_t emoji_1f614_64; // sad
extern const lv_image_dsc_t emoji_1f620_64; // angry
extern const lv_image_dsc_t emoji_1f62d_64; // crying
extern const lv_image_dsc_t emoji_1f60d_64; // loving
extern const lv_image_dsc_t emoji_1f633_64; // embarrassed
extern const lv_image_dsc_t emoji_1f62f_64; // surprised
extern const lv_image_dsc_t emoji_1f631_64; // shocked
extern const lv_image_dsc_t emoji_1f914_64; // thinking
extern const lv_image_dsc_t emoji_1f609_64; // winking
extern const lv_image_dsc_t emoji_1f60e_64; // cool
extern const lv_image_dsc_t emoji_1f60c_64; // relaxed
extern const lv_image_dsc_t emoji_1f924_64; // delicious
extern const lv_image_dsc_t emoji_1f618_64; // kissy
extern const lv_image_dsc_t emoji_1f60f_64; // confident
extern const lv_image_dsc_t emoji_1f634_64; // sleepy
extern const lv_image_dsc_t emoji_1f61c_64; // silly
extern const lv_image_dsc_t emoji_1f644_64; // confused

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
