#ifndef EMOJI_COLLECTION_H
#define EMOJI_COLLECTION_H

#include "lvgl_image.h"

#include <lvgl.h>

#include <map>
#include <string>
#include <memory>
#include <deque>
#include <algorithm>


// Define interface for emoji collection
class EmojiCollection {
public:
    virtual void AddEmoji(const std::string& name, LvglImage* image);
    virtual const LvglImage* GetEmojiImage(const char* name);
    virtual void LoadDefaultEmoji(const std::string& path);
    virtual ~EmojiCollection();

private:
    std::map<std::string, LvglImage*> emoji_collection_;
    std::deque<std::string> sd_emoji_lru_;
    const size_t max_sd_emojis_ = 2; // Keep at most 2 SD-based emojis in PSRAM
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
