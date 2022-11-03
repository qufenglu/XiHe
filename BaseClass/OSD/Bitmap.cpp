#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "Bitmap.h"
#include "Log/Log.h"

#define CHECK_ROW_AND_CLO(row,col) \
row >= m_nTableRow ? false :\
col >= m_nTableColumn ? false : true

const uint8_t Set0Table[8] = { 0xfe,0xfd,0xfb,0xf7,0xef,0xdf,0xbf,0x7f };

Bitmap::Bitmap()
{
    m_pBitTable = nullptr;
    m_nTableRow = 0;
    m_nTableColumn = 0;
    m_dAngle = .0;
    m_eAlignMode = ALIGN_BOTTOM;
}

Bitmap::~Bitmap()
{
    ReleaseAll();
}

int32_t Bitmap::ReleaseAll()
{
    if (m_pBitTable != nullptr)
    {
        for (uint32_t row = 0; row < m_nTableRow; row++)
        {
            free(m_pBitTable[row]);
        }
        free(m_pBitTable);
        m_pBitTable = nullptr;
    }

    m_nTableRow = 0;
    m_nTableColumn = 0;
    m_dAngle = .0;

    return 0;
}

int32_t Bitmap::Init(uint32_t row, uint32_t col)
{
    ReleaseAll();
    uint32_t nRow = row;
    uint32_t nCol = (col / 8) + 1;
    int32_t ret = 0;
    m_pBitTable = (uint8_t**)malloc(nRow * sizeof(uint8_t*));
    if (m_pBitTable == nullptr)
    {
        Error("[%p][Bitmap::Init] malloc  BitTable fail,row:%d", this, nRow);
        ret = -1;
        goto fail;
    }
    memset(m_pBitTable, 0, nRow * sizeof(uint8_t*));
    for (uint32_t row = 0; row < nRow; row++)
    {
        m_pBitTable[row] = (uint8_t*)malloc(nCol * sizeof(uint8_t));
        if (m_pBitTable[row] == nullptr)
        {
            Error("[%p][Bitmap::Init] malloc  column fail,column:%d", this, nCol);
            ret = -2;
            goto fail;
        }
        memset(m_pBitTable[row], 0, nCol * sizeof(uint8_t));
    }
    m_nTableRow = row;
    m_nTableColumn = col;

    return 0;
fail:
    ReleaseAll();
    return ret;
}

bool Bitmap::GetBit(uint32_t row, uint32_t col, uint8_t& bit)
{
    bool ret = CHECK_ROW_AND_CLO(row, col);
    if (!ret)
    {
        Error("[%p][Bitmap::Bitmap] get  bit fail,row:%d col:%d", this, row, col);
        return false;
    }

    uint8_t byte = m_pBitTable[row][col / 8];
    bit = byte >> (col % 8);
    bit &= 0x01;

    return true;
}

bool Bitmap::SetBit(uint32_t row, uint32_t col, uint8_t bit)
{
    bool ret = CHECK_ROW_AND_CLO(row, col);
    if (!ret)
    {
        Error("[%p][Bitmap::Bitmap] set  bit fail,row:%d col:%d", this, row, col);
        return false;
    }

    uint8_t* byte = &m_pBitTable[row][col / 8];
    if (bit == 0)
    {
        *byte &= Set0Table[col % 8];
    }
    else
    {
        *byte |= (0x01 << (col % 8));
    }
    return true;
}

int32_t Bitmap::Join(std::list<std::list<Bitmap*>*> lines, uint nColSpacing, uint nRowSpacing, uint nMinHight)
{
    uint nBitmapWidth = 0;
    uint nBitmapHeight = 0;

    for (auto& line : lines)
    {
        uint nLineWidth = 0;
        uint nLineHeight = nMinHight;
        for (auto& item : *line)
        {
            nLineWidth += item->GetColumnNum();
            nLineWidth += nColSpacing;
            if (nLineHeight < item->GetRowNum())
            {
                nLineHeight = item->GetRowNum();
            }
        }
        nLineHeight += nRowSpacing;

        if (nBitmapWidth < nLineWidth)
        {
            nBitmapWidth = nLineWidth;
        }
        nBitmapHeight += nLineHeight;
    }

    int32_t ret = Init(nBitmapHeight, nBitmapWidth);
    if (ret < 0)
    {
        Error("[%p][Bitmap::join] Init  Bitmap fail,return:%d", this, ret);
        return -1;
    }

    uint nOffsetX = 0;
    uint nOffsetY = 0;
    for (auto& line : lines)
    {
        uint nLineHeight = nMinHight;
        for (auto& item : *line)
        {
            if (nLineHeight < item->GetRowNum())
            {
                nLineHeight = item->GetRowNum();
            }
        }

        for (auto& item : *line)
        {
            AlignMode mode = item->GetAlignMode();
            uint nAlignY = 0;
            if (mode == ALIGN_MID)
            {
                nAlignY = (nLineHeight - item->GetRowNum()) / 2;
            }
            else if (mode == ALIGN_BOTTOM)
            {
                nAlignY = nLineHeight - item->GetRowNum();
            }
            SetBitmap(*item, nOffsetX, nOffsetY + nAlignY);

            nOffsetX += item->GetColumnNum();
            nOffsetX += nColSpacing;
        }

        nOffsetY += nLineHeight;
        nOffsetY += nRowSpacing;
    }

    return 0;
}

int32_t Bitmap::Join(Bitmap& bitmap)
{
    int32_t ret = Init(bitmap.GetRowNum(), bitmap.GetColumnNum());
    if (ret != 0)
    {
        Error("[%p][Bitmap::join] Init  Bitmap fail,return:%d", this, ret);
        return -1;
    }
    ret = SetBitmap(bitmap, 0, 0);
    if (ret != 0)
    {
        Error("[%p][Bitmap::join] SetBitmap fail,return:%d", this, ret);
        return -2;
    }

    return 0;
}

int32_t Bitmap::SetBitmap(Bitmap& bitmap, uint32_t x, uint32_t y)
{
    uint32_t xMax = bitmap.GetColumnNum() + x;
    uint32_t yMax = bitmap.GetRowNum() + y;
    if (xMax > this->GetColumnNum() || yMax > this->GetRowNum())
    {
        Error("[%p][Bitmap::SetBitmap] bitmap(w:%d h:%d) is too small to SetBitmap(w:%d h:%d x:%d y:%d)",
            this, this->GetColumnNum(), this->GetRowNum(), bitmap.GetColumnNum(), bitmap.GetRowNum(), x, y);
        return -1;
    }

    uint8_t bit = 0;
    uint32_t nColumnNum = bitmap.GetColumnNum();
    uint32_t nRowNum = bitmap.GetRowNum();
    for (uint32_t row = 0; row < nRowNum; row++)
    {
        for (uint32_t col = 0; col < nColumnNum; col++)
        {
            bitmap.GetBit(row, col, bit);
            this->SetBit(y + row, x + col, bit);
        }
    }

    return 0;
}

 void Bitmap::PrintfBitmap()
{
    uint32_t row = this->GetRowNum();
    uint32_t col = this->GetColumnNum();
    uint8_t bit = 0;
    for (uint32_t i = 0; i < row; i++)
    {
        for (uint32_t j = 0; j < col; j++)
        {
            this->GetBit(i, j, bit);
            if (bit)
            {
                printf("*");
            }
            else
            {
                printf(" ");
            }
        }
        printf("\n");
    }
}