#include "RtspParser.h"

void split(const std::string& src, std::vector<std::string>& result, const std::string& c)
{
    result.clear();
    std::string::size_type nBegPos, nEndPos;
    size_t len = src.length();
    nEndPos = src.find(c);
    nBegPos = 0;

    while (std::string::npos != nEndPos)
    {
        result.emplace_back(src.substr(nBegPos, nEndPos - nBegPos));
        nBegPos = nEndPos + c.size();
        nEndPos = src.find(c, nBegPos);
    }

    if (nBegPos != len)
    {
        result.emplace_back(src.substr(nBegPos));
    }
}

RtspParser::RtspParser()
{
}

RtspParser::~RtspParser()
{
}

uint32_t RtspParser::ParseRtspRequest(const std::string& strReq, RtspParser::RtspRequest& req)
{
    req.m_RtspMethod = RTSP_METHOD_NONE;

    std::vector<std::string> lines;
    split(strReq, lines, "\r\n");
    std::size_t nLineSize = lines.size();
    if (nLineSize < 2)
    {
        return -1;
    }

    std::string strMethodLine = lines[0];
    std::vector<std::string> elements;
    split(strMethodLine, elements, " ");
    if (elements.size() < 3)
    {
        return -2;
    }
    RtspMethod method = elements[0] == "DESCRIBE" ? RTSP_METHOD_DESCRIBE :
        elements[0] == "ANNOUNCE" ? RTSP_METHOD_ANNOUNCE :
        elements[0] == "GET_PARAMETER" ? RTSP_METHOD_GET_PARAMETER :
        elements[0] == "OPTIONS" ? RTSP_METHOD_OPTIONS :
        elements[0] == "PAUSE" ? RTSP_METHOD_PAUSE :
        elements[0] == "PLAY" ? RTSP_METHOD_PLAY :
        elements[0] == "RECORD" ? RTSP_METHOD_RECORD :
        elements[0] == "REDIRECT" ? RTSP_METHOD_REDIRECT :
        elements[0] == "SETUP" ? RTSP_METHOD_SETUP :
        elements[0] == "SET_PARAMETER" ? RTSP_METHOD_SET_PARAMETER :
        elements[0] == "TEARDOWN" ? RTSP_METHOD_TEARDOWN : RTSP_METHOD_NONE;
    if (method == RTSP_METHOD_NONE)
    {
        return -3;
    }
    req.m_RtspMethod = method;
    req.m_StrUrl = elements[1];
    req.m_StrVersion = elements[2];

    for (std::size_t i = 1; i < nLineSize - 1; i++)
    {
        elements.clear();
        split(lines[i], elements, ": ");
        if (elements.size() >= 2)
        {
            req.m_FieldsMap[elements[0]] = elements[1];
        }
    }

    std::string strLastLine = lines[nLineSize - 1];
    if (req.m_FieldsMap.find("Content-type") != req.m_FieldsMap.end())
    {
        req.m_StrContent = strLastLine;
    }
    else
    {
        elements.clear();
        split(strLastLine, elements, ": ");
        if (elements.size() >= 2)
        {
            req.m_FieldsMap[elements[0]] = elements[1];
        }
    }

    return 0;
}

uint32_t RtspParser::ParseRtspResponse(const std::string& strRsp, RtspParser::RtspResponse& rsp)
{
    std::vector<std::string> lines;
    split(strRsp, lines, "\r\n");
    std::size_t nLineSize = lines.size();
    if (nLineSize < 2)
    {
        return -1;
    }

    std::string strCodeLine = lines[0];
    std::vector<std::string> elements;
    split(strCodeLine, elements, " ");
    if (elements.size() < 3)
    {
        return -2;
    }
    rsp.m_StrVersion = elements[0];
    rsp.m_StrErrcode = elements[1];
    rsp.m_StrReason = elements[2];

    for (size_t i = 0; i < nLineSize; i++)
    {
        elements.clear();
        split(lines[i], elements, ": ");
        if (elements.size() >= 2)
        {
            rsp.m_FieldsMap[elements[0]] = elements[1];
        }
    }

    return 0;
}

uint32_t RtspParser::GetStrRequest(std::string& strReq, const RtspParser::RtspRequest& req)
{
    strReq = "";
    if (req.m_RtspMethod == RTSP_METHOD_NONE || req.m_StrUrl == "" || req.m_StrVersion == "")
    {
        return -1;
    }

    std::string method = req.m_RtspMethod == RTSP_METHOD_DESCRIBE ? "DESCRIBE" :
        req.m_RtspMethod == RTSP_METHOD_ANNOUNCE ? "ANNOUNCE" :
        req.m_RtspMethod == RTSP_METHOD_GET_PARAMETER ? "GET_PARAMETER" :
        req.m_RtspMethod == RTSP_METHOD_OPTIONS ? "OPTIONS" :
        req.m_RtspMethod == RTSP_METHOD_PAUSE ? "PAUSE" :
        req.m_RtspMethod == RTSP_METHOD_PLAY ? "PLAY" :
        req.m_RtspMethod == RTSP_METHOD_RECORD ? "RECORD" :
        req.m_RtspMethod == RTSP_METHOD_REDIRECT ? "REDIRECT" :
        req.m_RtspMethod == RTSP_METHOD_SETUP ? "SETUP" :
        req.m_RtspMethod == RTSP_METHOD_SET_PARAMETER ? "SET_PARAMETER" :
        req.m_RtspMethod == RTSP_METHOD_TEARDOWN ? "TEARDOWN" : "";
    if (method == "")
    {
        return -2;
    }

    strReq += method;
    strReq += " ";
    strReq += req.m_StrUrl;
    strReq += " ";
    strReq += req.m_StrVersion;
    strReq += "\r\n";

    for (auto& field : req.m_FieldsMap)
    {
        strReq += field.first;
        strReq += ": ";
        strReq += field.second;
        strReq += "\r\n";
    }

    size_t nContentSize = req.m_StrContent.size();
    if (nContentSize > 0)
    {
        strReq += "Content-length: ";
        strReq += std::to_string(nContentSize);
        strReq += "\r\n";
    }
    strReq += "\r\n";

    if (nContentSize > 0)
    {
        strReq += req.m_StrContent;
    }

    return 0;
}

uint32_t RtspParser::GetStrResponse(std::string& strRsp, const RtspParser::RtspResponse& rsp)
{
    strRsp = "";
    if (rsp.m_StrErrcode == "" || rsp.m_StrReason == "" || rsp.m_StrVersion == "")
    {
        return -1;
    }

    strRsp += rsp.m_StrVersion;
    strRsp += " ";
    strRsp += rsp.m_StrErrcode;
    strRsp += " ";
    strRsp += rsp.m_StrReason;
    strRsp += "\r\n";

    for (auto& field : rsp.m_FieldsMap)
    {
        strRsp += field.first;
        strRsp += ": ";
        strRsp += field.second;
        strRsp += "\r\n";
    }

    size_t nContentSize = rsp.m_StrContent.size();
    if (nContentSize > 0)
    {
        strRsp += "Content-length: ";
        strRsp += std::to_string(nContentSize);
        strRsp += "\r\n";
    }
    strRsp += "\r\n";

    if (nContentSize > 0)
    {
        strRsp += rsp.m_StrContent;
    }

    return 0;
}
