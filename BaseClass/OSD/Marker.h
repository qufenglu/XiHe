#pragma once
#include <string>
#include <mutex>
#include <memory>
#include "Common.h"
#include "Bitmap.h"
extern"C"
{
#include <freetype/freetype.h>
}

class Marker
{
public:
    typedef struct Color
    {
        uint8_t R;
        uint8_t G;
        uint8_t B;
        uint8_t A;
        Color()
        {
            R = 255;
            G = 255;
            B = 255;
            A = 0;
        }
    }Color;

public:
    Marker();
    ~Marker();
    int32_t Init();
    int32_t AddKey(const std::string& key, const Color& clolr, int32_t x, int32_t y);
    int32_t AddValue(const std::string& value, const Color& clolr, int32_t x, int32_t y);
    int32_t AddKey(Bitmap& key, const Color& clolr, int32_t x, int32_t y);
    int32_t AddValue(Bitmap& value, const Color& clolr, int32_t x, int32_t y);
    int32_t AddMarker2VideoFrame(const std::shared_ptr<VideoFrame>& farme);
//for test
    void PrintfMark();

private:
    int32_t ReleaseAll();
    int32_t GetBitmaps(std::list<std::list<Bitmap*>*>& lines, const std::string& key);

private:
    std::string m_strFont;
    std::string m_strKey;
    Bitmap* m_pKeyBitmap;
    Color m_KeyColor;
    int32_t m_nKeyLoctionX;
    int32_t m_nKeyLoctionY;
    std::string m_strValue;
    Bitmap* m_pValueBitmap;
    Color m_ValueColor;
    int32_t m_nValueLoctionX;
    int32_t m_nValueLoctionY;

    FT_Library m_pFTLibrary;
    FT_Face m_pFTFace;
};