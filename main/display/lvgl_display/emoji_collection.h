#ifndef EMOJI_COLLECTION_H
#define EMOJI_COLLECTION_H

#include "lvgl_image.h"

#include <lvgl.h>

#include <map>
#include <string>
#include <memory>


// Define interface for emoji collection
class EmojiCollection {
public:
    virtual void AddEmoji(const std::string& name, LvglImage* image);
    virtual const LvglImage* GetEmojiImage(const char* name);
    virtual const LvglImage* GetRandomVariant(const char* name);
    virtual ~EmojiCollection();

    // Load all emojis from SD card folder
    // Scans /sdcard/dodomio/emoji/ and loads all .gif files
    void LoadDefaultEmoji(const char* base_path = "/sdcard/dodomio/emoji");
    
    // Load single emoji on-demand when server requests it
    // Returns true if loaded successfully
    bool LoadEmoji(const char* name, const char* base_path = "/sdcard/dodomio/emoji");

private:
    std::map<std::string, LvglImage*> emoji_collection_;
};

class Twemoji32 : public EmojiCollection {
public:
    Twemoji32();
};

class Twemoji64 : public EmojiCollection {
public:
    Twemoji64();
};

#endif
