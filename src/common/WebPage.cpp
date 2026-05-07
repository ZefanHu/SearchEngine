#include "WebPage.h"
#include "SplitToolCppJieba.h"
#include <algorithm>
#include <iostream>
#include <regex>

WebPage::WebPage(const std::string &page)
    : _format_xml(page), _id(0), _figure_print(0)
{
}

void WebPage::unrepeatedWebPageLibHelper(simhash::Simhasher *p_simhasher)
{
    extractWebPage();
    generateFigurePrint(p_simhasher);
}

void WebPage::invertIndexLibHelper(SplitTool *p_split_tool)
{
    extractWebPage();
    generateWordFrequenceMap(p_split_tool);
}

int WebPage::getID() const { return _id; }
const std::string &WebPage::getTitle() const { return _title; }
const std::string &WebPage::getURL() const { return _url; }
const std::string &WebPage::getContent() const { return _content; }
const std::map<std::string, int> &WebPage::getWordFrequenceMap() const { return _word_frequence; }
uint64_t WebPage::getFigurePrint() const { return _figure_print; }

void WebPage::extractWebPage()
{
    size_t docid_begin = _format_xml.find("<docid>");
    size_t docid_end = _format_xml.find("</docid>");
    size_t title_begin = _format_xml.find("<title>");
    size_t title_end = _format_xml.find("</title>");
    size_t link_begin = _format_xml.find("<link>");
    size_t link_end = _format_xml.find("</link>");
    size_t content_begin = _format_xml.find("<content>");
    size_t content_end = _format_xml.find("</content>");

    if (docid_begin != std::string::npos && docid_end != std::string::npos)
    {
        std::string docid_str = _format_xml.substr(docid_begin + 7, docid_end - docid_begin - 7);
        _id = std::stoi(docid_str);
    }

    // 同样处理标题
    if (title_begin != std::string::npos && title_end != std::string::npos)
    {
        _title = _format_xml.substr(title_begin + 7, title_end - title_begin - 7);
        removeHtmlTags(_title); // 在这里调用新方法
    }

    if (link_begin != std::string::npos && link_end != std::string::npos)
    {
        _url = _format_xml.substr(link_begin + 6, link_end - link_begin - 6);
    }

    if (content_begin != std::string::npos && content_end != std::string::npos)
    {
        _content = _format_xml.substr(content_begin + 9, content_end - content_begin - 9);
        removeHtmlTags(_content); // 在这里调用新方法
    }

    // 添加调试信息
    // std::cout << "Extracted page: ID=" << _id
    //           << ", Title=" << _title.substr(0, 30) << "..."
    //           << ", URL=" << _url
    //           << ", Content length=" << _content.length() << std::endl;
}

void WebPage::generateWordFrequenceMap(SplitTool *p_split_tool)
{
    std::string page_content(_content);
    extractChineseWord(page_content);

    std::vector<std::string> split_res = p_split_tool->cut(page_content);

    for (const auto &word : split_res)
    {
        if (!word.empty() && !isStopWord(word))
        {
            ++_word_frequence[word];
        }
    }
}

void WebPage::generateFigurePrint(simhash::Simhasher *p_simhasher)
{
    p_simhasher->make(_content, 10, _figure_print);
}

bool WebPage::isStopWord(const std::string &word) const
{
    const auto &stop_set = Configuration::getInstance().getStopWordSet();
    return stop_set.find(word) != stop_set.end();
}

void WebPage::extractChineseWord(std::string &word)
{
    word.erase(std::remove_if(word.begin(), word.end(),
                              [](char ch)
                              { return !isChinese(ch); }),
               word.end());
}

bool WebPage::isChinese(char ch)
{
    return (ch & 0x80) != 0;
}

size_t WebPage::nBytesCode(char ch)
{
    if (ch & 0x80)
    {
        int bytes = 1;
        for (int i = 0; i < 6; ++i)
        {
            if (ch & (1 << (6 - i)))
                ++bytes;
            else
                break;
        }
        return bytes;
    }
    return 1;
}

void WebPage::removeHtmlTags(std::string &content)
{
    // 使用正则表达式去除HTML标签和特殊字符
    std::regex htmlTags("<[^>]*>");
    content = std::regex_replace(content, htmlTags, "");

    // 处理特定的字符实体，例如 &nbsp;
    std::regex entities("&[^;]+;");
    content = std::regex_replace(content, entities, " ");
}

bool operator==(const WebPage &lhs, const WebPage &rhs)
{
    return lhs._id == rhs._id;
}

bool operator!=(const WebPage &lhs, const WebPage &rhs)
{
    return !(lhs == rhs);
}

bool operator<(const WebPage &lhs, const WebPage &rhs)
{
    return lhs._id < rhs._id;
}
