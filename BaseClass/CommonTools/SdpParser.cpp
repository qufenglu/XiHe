#include <vector>
#include "SdpParser.h"
#include "Log/Log.h"

extern void split(const std::string& src, std::vector<std::string>& result, const std::string& c);

SdpParser::SdpParser()
{
}

SdpParser::~SdpParser()
{
}

int32_t SdpParser::Parse(const std::string& sdp)
{
    size_t pos;
    std::vector<std::string> lines;
    split(sdp, lines, "\r\n");

    SdpParser::Description description;
    SdpParser::MediaDescription mediaDescription;
    bool bIsMediaDescription = false;
    int32_t ret = 0;

    for (auto line : lines)
    {
        pos = line.find('=');
        if (pos == std::string::npos)
        {
            Error("[%p][SdpParser::Parse] Parse error line:%s", this, line.c_str());
            continue;
        }

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        if (key == "m")
        {
            if (description.field.size() > 0)
            {
                if (bIsMediaDescription)
                {
                    ret = ParseMediaInfo(mediaDescription);
                    if (ret != 0)
                    {
                        Error("[%p][SdpParser::Parse] ParseMediaInfo error media:%s", this, mediaDescription.field["m"].c_str());
                    }
                    else
                    {
                        m_MediaDescription.push_back(mediaDescription);
                    }
                }
                else
                {
                    m_SessionDescription = description;
                }
                description.field.clear();
            }

            bIsMediaDescription = true;
        }
        else if (key == "a")
        {
            if (!bIsMediaDescription)
            {
                Error("[%p][SdpParser::Parse] Parse error line:%s", this, line.c_str());
            }
            pos = value.find(":");
            if (pos == std::string::npos)
            {
                Error("[%p][SdpParser::Parse] Parse error line:%s", this, line.c_str());
            }

            key = value.substr(0, pos);
            value = value.substr(pos + 1);
        }

        if (bIsMediaDescription)
        {
            mediaDescription.field[key] = value;
        }
        else
        {
            description.field[key] = value;
        }
    }

    if (mediaDescription.field.size() > 0)
    {
        ret = ParseMediaInfo(mediaDescription);
        if (ret != 0)
        {
            Error("[%p][SdpParser::Parse] ParseMediaInfo error media:%s", this, mediaDescription.field["m"].c_str());
        }
        else
        {
            m_MediaDescription.push_back(mediaDescription);
        }
    }

    return 0;
}

int32_t SdpParser::ParseMediaInfo(MediaDescription& description)
{
    std::string& media = description.field.at("m");
    if (description.field.find("rtpmap") == description.field.end())
    {
        Error("[%p][SdpParser::ParseMediaInfo] can not find rtpmap media:%s", this, media.c_str());
        return -1;
    }

    std::vector<std::string> rtpmapItems;
    split(description.field["rtpmap"], rtpmapItems, " ");
    if (rtpmapItems.size() < 2)
    {
        Error("[%p][SdpParser::ParseMediaInfo] rtpmap media:%s error", this, media.c_str());
        return -2;
    }
    description.nPayloadType = atoi(rtpmapItems[0].c_str());
    std::vector<std::string> mediaInfo;
    split(rtpmapItems[1], mediaInfo, "/");
    if (mediaInfo.size() < 2)
    {
        Error("[%p][SdpParser::ParseMediaInfo]  can not parse meaid info", this, media.c_str());
        return -3;
    }
    description.strMediaFormat = mediaInfo[0];
    description.nClockRate = atoi(mediaInfo[1].c_str());

    if (description.field.find("control") == description.field.end())
    {
        Error("[%p][SdpParser::ParseMediaInfo] can not find control media:%s error", this, media.c_str());
        return  -4;
    }
    std::vector<std::string> trackInfo;
    split(description.field["control"], trackInfo, "=");
    if (trackInfo.size() < 2)
    {
        Error("[%p][SdpParser::ParseMediaInfo]  parse control fail,media:%s", this, media.c_str());
        return -5;
    }
    description.nTrackID = atoi(trackInfo[1].c_str());

    if (description.field.find("range") != description.field.end())
    {
        description.strRange = description.field["range"];
    }

    return 0;
}

const SdpParser::Description& SdpParser::GetSessionDescription()
{
    return m_SessionDescription;
}

const std::list<SdpParser::MediaDescription>& SdpParser::GetMediaDescription()
{
    return m_MediaDescription;
}
