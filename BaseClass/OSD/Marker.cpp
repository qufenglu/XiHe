#include <list>
#include "Marker.h"
#include "Log/Log.h"
extern"C"
{
#include <ft2build.h>
#include FT_FREETYPE_H
}

//std::string g_strFont = "msyh.ttc";
std::string g_strFont = "simkai.ttf";
std::mutex g_strFontLock;
const std::string strFontFolder = "/usr/";

Marker::Marker()
{
    m_pKeyBitmap = nullptr;
    m_nKeyLoctionX = 0;
    m_nKeyLoctionY = 0;
    m_pValueBitmap = nullptr;
    m_nValueLoctionX = 0;
    m_nValueLoctionY = 0;
    {
        std::lock_guard<std::mutex> lock(g_strFontLock);
        m_strFont = g_strFont;
    }
    m_pFTFace = nullptr;
    m_pFTLibrary = nullptr;
}

Marker::~Marker()
{
    ReleaseAll();
}

int32_t Marker::Init()
{
    ReleaseAll();
    std::string path = strFontFolder + g_strFont;
    int32_t ret = 0;
    float  lean = 0.3f;
    FT_Matrix matrix;

    FT_Error err = FT_Init_FreeType(&m_pFTLibrary);
    if (err != FT_Err_Ok)
    {
        Error("[%p][Marker::Init] FT_Init_FreeType fail,return:%d", this, err);
        ret = -1;
        goto fail;
    }
    err = FT_New_Face(m_pFTLibrary, path.c_str(), 0, &m_pFTFace);
    if (err != FT_Err_Ok)
    {
        Error("[%p][Marker::Init] FT_New_Face fail,return:%d", this, err);
        ret = -2;
        goto fail;
    }
    //err = FT_Set_Pixel_Sizes(m_pFTFace,16, 16);
    err = FT_Set_Char_Size(m_pFTFace, 14 * 64, 14 * 64, 96, 96);
    if (err != FT_Err_Ok)
    {
        Error("[%p][Marker::Init] FT_Set_Char_Size fail,return:%d", this, err);
        ret = -3;
        goto fail;
    }

    matrix.xx = 0x10000L;
    matrix.xy = lean * 0x10000L;
    matrix.yx = 0;
    matrix.yy = 0x10000L;
    FT_Set_Transform(m_pFTFace, &matrix, 0);

    return ret;
fail:
    ReleaseAll();
    return ret;
}

int32_t Marker::ReleaseAll()
{
    delete m_pKeyBitmap;
    m_pKeyBitmap = nullptr;
    delete m_pValueBitmap;
    m_pValueBitmap = nullptr;
    m_nKeyLoctionX = 0;
    m_nKeyLoctionY = 0;
    m_nValueLoctionX = 0;
    m_nValueLoctionY = 0;

    if (m_pFTFace != nullptr)
    {
        FT_Done_Face(m_pFTFace);
        m_pFTFace = nullptr;
    }
    if (m_pFTLibrary != nullptr)
    {
        FT_Done_FreeType(m_pFTLibrary);
        m_pFTLibrary = nullptr;
    }

    return 0;
}

int32_t Marker::GetBitmaps(std::list<std::list<Bitmap*>*>& lines, const std::string& key)
{
    if (m_pFTFace == nullptr)
    {
        Error("[%p][Marker::GetBitmaps] FTFace is null ", this);
        return -1;
    }

    size_t size = mbstowcs(nullptr, key.c_str(), 0) + 1;
    if (size <= 0)
    {
        Error("[%p][Marker::GetBitmaps] mbstowcs %s fali", this, key.c_str());
        return -2;
    }
    wchar_t* wchar = (wchar_t*)malloc(size * sizeof(wchar_t));
    if (wchar == nullptr)
    {
        Error("[%p][Marker::GetBitmaps] malloc  wchar:%d fali", this, size);
        return -3;
    }

    std::list<Bitmap*>* line = nullptr;
    mbstowcs(wchar, key.c_str(), key.length() + 1);
    for (size_t i = 0; i < size - 1; i++)
    {
        if (line == nullptr)
        {
            line = new  std::list<Bitmap*>();
        }

        if (wchar[i] == L'\r')
        {
            continue;
        }
        else if (wchar[i] == L'\n')
        {
            lines.push_back(line);
            line = nullptr;
        }
        else
        {
            uint index = FT_Get_Char_Index(m_pFTFace, wchar[i]);
            FT_Load_Glyph(m_pFTFace, index, FT_LOAD_DEFAULT);
            if (m_pFTFace->glyph->format != FT_GLYPH_FORMAT_BITMAP)
            {
                FT_Render_Glyph(m_pFTFace->glyph, FT_RENDER_MODE_NORMAL);
            }

            uint row = m_pFTFace->glyph->bitmap.rows;
            uint col = m_pFTFace->glyph->bitmap.width;
            uint8_t* buff = m_pFTFace->glyph->bitmap.buffer;
            int pitch = m_pFTFace->glyph->bitmap.pitch;
            if (pitch < 0)
            {
                pitch = 0 - pitch;
            }
            Bitmap* map = new Bitmap();
            int ret = map->Init(row, col);
            if (ret != 0)
            {
                Error("[%p][Marker::AddKey] Init  bitmap fail,row:%d col:%d", this, row, col);
                delete map;
                free(wchar);
                return -4;
            }

            for (uint i = 0; i < row; i++)
            {
                int offset = i * pitch;
                for (uint j = 0; j < col; j++)
                {
                    map->SetBit(i, j, buff[offset + j]);
                }
            }

            if ((wchar[i] >= L'A' && wchar[i] <= L'Z') || (wchar[i] >= L'a' && wchar[i] <= L'z') || (wchar[i] == L'.'))
            {
                map->SetAlignMode(ALIGN_BOTTOM);
            }
            else
            {
                map->SetAlignMode(ALIGN_MID);
            }

            line->push_back(map);
        }
    }
    if (line != nullptr)
    {
        lines.push_back(line);
    }

    free(wchar);
    return 0;
}

void Clearlines(std::list<std::list<Bitmap*>*>& lines)
{
    for (auto& line : lines)
    {
        for (auto& item : *line)
        {
            delete item;
        }
        line->clear();
        delete line;
    }
    lines.clear();
}

int32_t Marker::AddKey(const std::string& key, const Color& clolr, int32_t x, int32_t y)
{
    if (m_strKey != key)
    {
        std::list<std::list<Bitmap*>*> lines;
        int32_t ret = GetBitmaps(lines, key);
        if (ret != 0)
        {
            Clearlines(lines);
            Error("[%p][Marker::AddKey] GetBitmaps fail,return:%d ", this, ret);
            return -1;
        }

        if (m_pKeyBitmap == nullptr)
        {
            m_pKeyBitmap = new Bitmap();
        }
        ret = m_pKeyBitmap->Join(lines);
        Clearlines(lines);
        if (ret != 0)
        {
            Error("[%p][Marker::AddKey] Join Bitmaps fail,return:%d ", this, ret);
            return -2;
        }
        m_strKey = key;
    }

    m_nKeyLoctionX = x;
    m_nKeyLoctionY = y;
    m_KeyColor = clolr;

    return 0;
}

int32_t Marker::AddValue(const std::string& value, const Color& clolr, int32_t x, int32_t y)
{
    if (m_strValue != value)
    {
        std::list<std::list<Bitmap*>*> lines;
        int32_t ret = GetBitmaps(lines, value);
        if (ret != 0)
        {
            Clearlines(lines);
            Error("[%p][Marker::AddValue] GetBitmaps fail,return:%d ", this, ret);
            return -1;
        }

        if (m_pValueBitmap == nullptr)
        {
            m_pValueBitmap = new Bitmap();
        }
        ret = m_pValueBitmap->Join(lines);
        Clearlines(lines);
        if (ret != 0)
        {
            Error("[%p][Marker::AddValue] Join Bitmaps fail,return:%d ", this, ret);
            return -2;
        }
        m_strValue = value;
    }

    m_nValueLoctionX = x;
    m_nValueLoctionY = y;
    m_ValueColor = clolr;

    return 0;
}

int32_t Marker::AddKey(Bitmap& key, const Color& clolr, int32_t x, int32_t y)
{
    if (m_pKeyBitmap == nullptr)
    {
        m_pKeyBitmap = new Bitmap();
    }

    int32_t ret = m_pKeyBitmap->Join(key);
    if (ret != 0)
    {
        Error("[%p][Marker::AddKey] Join Bitmaps fail,return:%d ", this, ret);
        return -1;
    }

    m_strKey = "";
    m_nKeyLoctionX = x;
    m_nKeyLoctionY = y;
    m_KeyColor = clolr;

    return 0;
}

int32_t Marker::AddValue(Bitmap& value, const Color& clolr, int32_t x, int32_t y)
{
    if (m_pValueBitmap == nullptr)
    {
        m_pValueBitmap = new Bitmap();
    }

    int32_t ret = m_pValueBitmap->Join(value);
    if (ret != 0)
    {
        Error("[%p][Marker::AddValue] Join Bitmaps fail,return:%d ", this, ret);
        return -1;
    }

    m_strKey = "";
    m_nValueLoctionX = x;
    m_nValueLoctionY = y;
    m_ValueColor = clolr;

    return 0;
}

bool AddBitmap2VideoFrame(const std::shared_ptr<VideoFrame>& farme, int32_t x, int32_t y,
    Bitmap* map, const Marker::Color& color)
{
    uint32_t col = map->GetColumnNum();
    uint32_t row = map->GetRowNum();
    uint8_t bit = 0;

    uint8_t Y = 0.299 * color.R + 0.587 * color.G + 0.114 * color.B;
    uint8_t U = -0.1687 * color.R - 0.3313 * color.G + 0.5 * color.B + 128;
    uint8_t V = 0.5 * color.R - 0.4187 * color.G - 0.0813 * color.B + 128;

    uint8_t* pY = farme->m_pData;
    uint8_t* pU = farme->m_pData + (farme->m_nWidth * farme->m_nHeight);
    uint8_t* pV = farme->m_pData + (farme->m_nWidth * farme->m_nHeight * 5 / 4);

    uint8_t* pPixY = nullptr;
    uint8_t* pPixU = nullptr;
    uint8_t* pPixV = nullptr;

    int32_t offsetY = 0;
    int32_t offsetUV = 0;

    for (uint32_t i = 0; i < row; i++)
    {
        uint32_t line = i + y;
        if (line > farme->m_nHeight)
        {
            break;
        }

        for (uint32_t j = 0; j < col; j++)
        {
            if ((x + j) > farme->m_nWidth)
            {
                continue;
            }

            offsetY = line * farme->m_nWidth;
            offsetUV = (line >> 1) * (farme->m_nWidth >> 1);

            map->GetBit(i, j, bit);
            if (bit != 0)
            {
                pPixY = pY + offsetY + (x + j);
                pPixU = pU + offsetUV + ((x + j) >> 1);
                pPixV = pV + offsetUV + ((x + j) >> 1);
                *pPixY = Y; *pPixU = U; *pPixV = V;
                //*pPixY = ((255 - color.A) * Y + (color.A * (*pPixY))) / 255;
                //*pPixU = ((255 - color.A) * U + (color.A * (*pPixU))) / 255;
                //*pPixV = ((255 - color.A) * V + (color.A * (*pPixV))) / 255;
            }
        }
    }

    return true;
}

int32_t Marker::AddMarker2VideoFrame(const std::shared_ptr<VideoFrame>& farme)
{
    int offsetX = 0;
    if (m_pKeyBitmap != nullptr)
    {
        AddBitmap2VideoFrame(farme, m_nKeyLoctionX, m_nKeyLoctionY, m_pKeyBitmap, m_KeyColor);
        offsetX = m_pKeyBitmap->GetColumnNum();
    }
    if (m_pValueBitmap != nullptr)
    {
        int32_t nValueOffsetX = m_nKeyLoctionX + offsetX;
        int32_t nValueOffsetY = m_nKeyLoctionY;
        AddBitmap2VideoFrame(farme, m_nValueLoctionX + nValueOffsetX,
            m_nValueLoctionY + nValueOffsetY, m_pValueBitmap, m_ValueColor);
    }

    return 0;
}

void Marker::PrintfMark()
{
    printf("key:\n");
    if (m_pKeyBitmap != nullptr)
    {
        m_pKeyBitmap->PrintfBitmap();
    }
    printf("value:\n");
    if (m_pValueBitmap != nullptr)
    {
        m_pValueBitmap->PrintfBitmap();
    }
}