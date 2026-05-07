#pragma once

#include <lvgl.h>
#include <string>


// Wrap around lv_img_dsc_t
class LvglImage {
public:
    virtual const lv_image_dsc_t* image_dsc() const = 0;
    virtual bool IsGif() const { return false; }
    virtual ~LvglImage() = default;
};


class LvglRawImage : public LvglImage {
public:
    LvglRawImage(void* data, size_t size);
    virtual const lv_image_dsc_t* image_dsc() const override { return &image_dsc_; }
    virtual bool IsGif() const;

private:
    lv_image_dsc_t image_dsc_;
};

class LvglCBinImage : public LvglImage {
public:
    LvglCBinImage(void* data);
    virtual ~LvglCBinImage();
    virtual const lv_image_dsc_t* image_dsc() const override { return image_dsc_; }

private:
    lv_image_dsc_t* image_dsc_ = nullptr;
};

class LvglSourceImage : public LvglImage {
public:
    LvglSourceImage(const lv_image_dsc_t* image_dsc) : image_dsc_(image_dsc) {}
    virtual const lv_image_dsc_t* image_dsc() const override { return image_dsc_; }

private:
    const lv_image_dsc_t* image_dsc_;
};

class LvglAllocatedImage : public LvglImage {
public:
    LvglAllocatedImage(void* data, size_t size);
    LvglAllocatedImage(void* data, size_t size, int width, int height, int stride, int color_format);
    virtual ~LvglAllocatedImage();
    virtual const lv_image_dsc_t* image_dsc() const override { return &image_dsc_; }

private:
    lv_image_dsc_t image_dsc_;
};

class LvglFileImage : public LvglImage {
public:
    LvglFileImage(const std::string& path) : path_(path) {}
    virtual const lv_image_dsc_t* image_dsc() const override { return nullptr; }
    virtual bool IsGif() const override { 
        return path_.find(".gif") != std::string::npos || path_.find(".GIF") != std::string::npos;
    }
    const std::string& path() const { return path_; }

private:
    std::string path_;
};