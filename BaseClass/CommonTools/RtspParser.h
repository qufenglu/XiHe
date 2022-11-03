#pragma once
#include <string>
#include <map>
#include <cstdint>
#include <vector>

void split(const std::string& src, std::vector<std::string>& result, const std::string& c);

class RtspParser
{
public:
    typedef enum RtspMethod
    {
        RTSP_METHOD_NONE = -1,
        RTSP_METHOD_DESCRIBE,
        RTSP_METHOD_ANNOUNCE,
        RTSP_METHOD_GET_PARAMETER,
        RTSP_METHOD_OPTIONS,
        RTSP_METHOD_PAUSE,
        RTSP_METHOD_PLAY,
        RTSP_METHOD_RECORD,
        RTSP_METHOD_REDIRECT,
        RTSP_METHOD_SETUP,
        RTSP_METHOD_SET_PARAMETER,
        RTSP_METHOD_TEARDOWN
    }RtspMethod;
    typedef struct RtspRequest
    {
        RtspMethod m_RtspMethod;
        std::string m_StrUrl;
        std::string m_StrVersion;
        std::map<std::string, std::string> m_FieldsMap;
        std::string m_StrContent;

        RtspRequest()
        {
            m_RtspMethod = RTSP_METHOD_NONE;
        }
    }RtspRequest;
    typedef struct RtspResponse
    {
        std::string m_StrVersion;
        std::string m_StrErrcode;
        std::string m_StrReason;
        std::map<std::string, std::string> m_FieldsMap;
        std::string m_StrContent;
    }RtspResponse;

public:
    RtspParser();
    ~RtspParser();

    static uint32_t ParseRtspRequest(const std::string& strReq, RtspParser::RtspRequest& req);
    static uint32_t ParseRtspResponse(const std::string& strRsp, RtspParser::RtspResponse& rsp);
    static uint32_t GetStrRequest(std::string& strReq, const RtspParser::RtspRequest& req);
    static uint32_t GetStrResponse(std::string& strRsp, const RtspParser::RtspResponse& rsp);

private:

};