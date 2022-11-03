#pragma once
#include <string>
#include <list>
#include <unordered_map>

class SdpParser
{
    typedef struct Description
    {
        std::unordered_map<std::string, std::string> field;
    }Description;

    typedef struct  MediaDescription
    {
        int32_t nTrackID;
        std::string strRange;
        uint8_t nPayloadType;
        std::string strMediaFormat;
        int32_t nClockRate;
        std::unordered_map<std::string, std::string> field;

        MediaDescription()
        {
            nTrackID = -1;
            nPayloadType = 96;
            nClockRate = 0;
        }
    }MediaDescription;

public:
    SdpParser();
    ~SdpParser();
    int32_t Parse(const std::string& sdp);
    const Description& GetSessionDescription();
    const std::list<SdpParser::MediaDescription>& GetMediaDescription();

private:
    int32_t ParseMediaInfo(MediaDescription& description);

private:
    SdpParser::Description m_SessionDescription;
    std::list<SdpParser::MediaDescription> m_MediaDescription;
};
