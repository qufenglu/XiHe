#pragma once
#include <cstdint>
#include <list>

typedef enum  AlignMode
{
    ALIGN_NONE,
    ALIGN_TOP,
    ALIGN_MID,
    ALIGN_BOTTOM,
}AlignMode;

class Bitmap
{
public:
    Bitmap();
    ~Bitmap();
    int32_t ReleaseAll();
    int32_t Init(uint32_t row, uint32_t col);
    inline uint32_t GetRowNum() { return m_nTableRow; };
    inline uint32_t GetColumnNum() { return m_nTableColumn; };
    bool GetBit(uint32_t row, uint32_t col, uint8_t& bit);
    bool SetBit(uint32_t row, uint32_t col, uint8_t bit);
    int32_t Join(std::list<std::list<Bitmap*>*> lines, uint nColSpacing = 5, uint nRowSpacing = 5, uint nMinHight = 10);
    int32_t Join(Bitmap& bitmap);
    inline AlignMode GetAlignMode() { return m_eAlignMode; };
    inline void SetAlignMode(AlignMode mode) { m_eAlignMode = mode; };
//for test
    void PrintfBitmap();

private:
    int32_t SetBitmap(Bitmap& bitmap, uint32_t x, uint32_t y);

private:
    uint8_t** m_pBitTable;
    uint32_t m_nTableRow;
    uint32_t m_nTableColumn;
    double m_dAngle;
    AlignMode m_eAlignMode;
};
