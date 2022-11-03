#pragma once
#include <map>
#include <memory>
#include "Marker.h"
#include "Common.h"

class OSD
{
public:
    OSD();
    ~OSD();
    int32_t AddOSD2VideoFrame(const std::shared_ptr<VideoFrame>& farme);
    int32_t AddMarker(const std::string& name);
    int32_t RemoveMarker(const std::string& name);
    int32_t SetKey(const std::string& name, const std::string& key, const Marker::Color& clolr, int32_t x, int32_t y);  //x,y绝对位置
    int32_t SetValue(const std::string& name, const std::string& value, const Marker::Color& clolr, int32_t x, int32_t y);  //x,y相对于key的位置
    int32_t SetKey(const std::string& name, Bitmap& key, const Marker::Color& clolr, int32_t x, int32_t y);//x,y绝对位置
    int32_t SetValue(const std::string& name, Bitmap& value, const Marker::Color& clolr, int32_t x, int32_t y);//x,y相对于key的位置

private:
    int32_t ReleaseAll();
    std::mutex m_cOsdLock;

private:
    std::map<std::string, Marker*>  m_pMarkerMap;
};
