#include "OSD.h"
#include "Log/Log.h"

OSD::OSD()
{

}

OSD::~OSD()
{
    ReleaseAll();
}

int32_t OSD::ReleaseAll()
{
    for (auto item : m_pMarkerMap)
    {
        delete item.second;
    }
    m_pMarkerMap.clear();

    return 0;
}

int32_t OSD::AddMarker(const std::string& name)
{
    std::lock_guard<std::mutex> lock(m_cOsdLock);
    if (m_pMarkerMap.find(name) != m_pMarkerMap.end())
    {
        Error("[%p][OSD::AddMarker] already have marker:%s", this, name.c_str());
        return -1;
    }

    Marker* pMarker = new Marker();
    int ret = pMarker->Init();
    if (ret != 0)
    {
        delete pMarker;
        Error("[%p][OSD::AddMarker] Init  marker:%s fail,return:%d", this, name.c_str(), ret);
        return -2;
    }

    m_pMarkerMap[name] = pMarker;
    return 0;
}

int32_t OSD::RemoveMarker(const std::string& name)
{
    std::lock_guard<std::mutex> lock(m_cOsdLock);
    if (m_pMarkerMap.find(name) == m_pMarkerMap.end())
    {
        Error("[%p][OSD::RemoveMarker] no marker:%s", this, name.c_str());
        return -1;
    }

    Marker* pMarker = m_pMarkerMap[name];
    m_pMarkerMap.erase(name);
    delete pMarker;

    return 0;
}

int32_t OSD::SetKey(const std::string& name, const std::string& key, const Marker::Color& clolr, int32_t x, int32_t y)
{
    std::lock_guard<std::mutex> lock(m_cOsdLock);
    if (m_pMarkerMap.find(name) == m_pMarkerMap.end())
    {
        Error("[%p][OSD::SerKey] no marker:%s", this, name.c_str());
        return -1;
    }

    Marker* pMarker = m_pMarkerMap[name];
    int32_t ret = pMarker->AddKey(key, clolr, x, y);
    if (ret != 0)
    {
        Error("[%p][OSD::SerKey] add key to marker:%s fail,ret:%d", this, name.c_str(), ret);
        return -2;
    }

    return 0;
}

int32_t OSD::SetValue(const std::string& name, const std::string& value, const Marker::Color& clolr, int32_t x, int32_t y)
{
    std::lock_guard<std::mutex> lock(m_cOsdLock);
    if (m_pMarkerMap.find(name) == m_pMarkerMap.end())
    {
        Error("[%p][OSD::SetValue] no marker:%s", this, name.c_str());
        return -1;
    }

    Marker* pMarker = m_pMarkerMap[name];
    int32_t ret = pMarker->AddValue(value, clolr, x, y);
    if (ret != 0)
    {
        Error("[%p][OSD::SerKey] add value to marker:%s fail,ret:%d", this, name.c_str(), ret);
        return -2;
    }

    return 0;
}

int32_t OSD::SetKey(const std::string& name, Bitmap& key, const Marker::Color& clolr, int32_t x, int32_t y)
{
    std::lock_guard<std::mutex> lock(m_cOsdLock);
    if (m_pMarkerMap.find(name) == m_pMarkerMap.end())
    {
        Error("[%p][OSD::SerKey] no marker:%s", this, name.c_str());
        return -1;
    }

    Marker* pMarker = m_pMarkerMap[name];
    int32_t ret = pMarker->AddKey(key, clolr, x, y);
    if (ret != 0)
    {
        Error("[%p][OSD::SerKey] add key to marker:%s fail,ret:%d", this, name.c_str(), ret);
        return -2;
    }

    return 0;
}

int32_t OSD::SetValue(const std::string& name, Bitmap& value, const Marker::Color& clolr, int32_t x, int32_t y)
{
    std::lock_guard<std::mutex> lock(m_cOsdLock);
    if (m_pMarkerMap.find(name) == m_pMarkerMap.end())
    {
        Error("[%p][OSD::SetValue] no marker:%s", this, name.c_str());
        return -1;
    }

    Marker* pMarker = m_pMarkerMap[name];
    int32_t ret = pMarker->AddValue(value, clolr, x, y);
    if (ret != 0)
    {
        Error("[%p][OSD::SerKey] add value to marker:%s fail,ret:%d", this, name.c_str(), ret);
        return -2;
    }

    return 0;
}

int32_t OSD::AddOSD2VideoFrame(const std::shared_ptr<VideoFrame>& farme)
{
    int32_t ret = 0;
    std::lock_guard<std::mutex> lock(m_cOsdLock);
    for (auto item : m_pMarkerMap)
    {
        ret = item.second->AddMarker2VideoFrame(farme);
        if (ret != 0)
        {
            Error("[%p][OSD::AddOSD2VideoFrame] add marker:%s fail,return:%d", this, item.first.c_str(), ret);
        }
    }
    return 0;
}