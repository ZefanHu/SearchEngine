#ifndef PAGE_LIB_H
#define PAGE_LIB_H

#include "DirScanner.h"
#include "Configuration.h"
#include <vector>
#include <string>
#include <map>
#include <utility>
#include <tinyxml2.h>

using std::string;

class PageLib
{
public:
    PageLib();
    void create();
    void store();

private:
    void processWebPage(const string &file_path);
    void cleanContent(std::string &content);
    void cleanTitle(std::string &title);
    void getRssItem(tinyxml2::XMLElement *item_element, string &title, string &link, string &content);
    void deleteHTMLTag(string &content);
    string generateWebPage(const string &title, const string &link, const string &content);

private:
    DirScanner _dir_scanner;
    std::vector<string> _web_pages;
    std::map<int, std::pair<int, int>> _offset_lib; // {网页ID, {起始位置, 结束位置}}
    string _page_lib_path;
    string _offset_lib_path;
};

#endif // PAGE_LIB_H
