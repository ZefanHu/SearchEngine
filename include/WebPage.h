#ifndef WEB_PAGE_H
#define WEB_PAGE_H

#include "Configuration.h"
#include "SplitTool.h"
#include "simhash/Simhasher.hpp"
#include <string>
#include <map>
#include <cstdint>

class WebPage
{
public:
    explicit WebPage(const std::string &page);

    void unrepeatedWebPageLibHelper(simhash::Simhasher *p_simhasher);
    void invertIndexLibHelper(SplitTool *p_split_tool);

    int getID() const;
    const std::string &getContent() const;
    const std::string &getTitle() const;
    const std::string &getURL() const;
    const std::map<std::string, int> &getWordFrequenceMap() const;
    uint64_t getFigurePrint() const;
    void extractWebPage();

    friend bool operator==(const WebPage &lhs, const WebPage &rhs);
    friend bool operator!=(const WebPage &lhs, const WebPage &rhs);
    friend bool operator<(const WebPage &lhs, const WebPage &rhs);

private:
    void generateWordFrequenceMap(SplitTool *p_split_tool);
    void generateFigurePrint(simhash::Simhasher *p_simhasher);

    static size_t nBytesCode(char ch);
    static bool isChinese(char ch);
    static void extractChineseWord(std::string &word);
    bool isStopWord(const std::string &word) const;
    void removeHtmlTags(std::string &content);

private:
    int _id;
    uint64_t _figure_print;
    std::string _url;
    std::string _title;
    std::string _content;
    std::string _format_xml;
    std::map<std::string, int> _word_frequence;
};

#endif // WEB_PAGE_H
