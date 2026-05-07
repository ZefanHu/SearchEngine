#include "PageLib.h"
#include "Configuration.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>

PageLib::PageLib()
{
    const auto &config = Configuration::getInstance();
    _page_lib_path = config.getConfig("webpage.dat");
    _offset_lib_path = config.getConfig("offsetlib.dat");
}

void PageLib::create()
{
    const auto &config = Configuration::getInstance();
    std::string web_page_dir = config.getConfig("web_page_dir");

    _dir_scanner.traverse(web_page_dir);
    const auto &web_page_paths = _dir_scanner.getFiles();

    std::cout << "[INFO] : Create Web Page Lib begin...\n";
    for (const auto &path : web_page_paths)
    {
        processWebPage(path);
        std::cout << "\t-> " << path << " success\n";
    }
    std::cout << "[INFO] : Create Web Page Lib end...\n";

    std::cout << "[INFO] : Create Web Page Offset Lib begin...\n";
    int cur_page_begin_pos = 0;
    for (size_t i = 0; i < _web_pages.size(); ++i)
    {
        int cur_page_length = static_cast<int>(_web_pages[i].size());
        _offset_lib[i + 1] = {cur_page_begin_pos, cur_page_begin_pos + cur_page_length - 1};
        cur_page_begin_pos += cur_page_length;
    }
    std::cout << "[INFO] : Create Web Page Offset Lib end...\n";
}

void PageLib::store()
{
    std::ofstream ofs_web_page(_page_lib_path);
    std::ofstream ofs_offset_lib(_offset_lib_path);

    if (!ofs_web_page || !ofs_offset_lib)
    {
        throw std::runtime_error("Failed to open output files");
    }

    std::cout << "[INFO] : Store Web Page Lib begin...\n";
    std::cout << "\t-> " << _page_lib_path << "\n";
    for (const auto &page : _web_pages)
    {
        ofs_web_page << page;
    }
    std::cout << "[INFO] : Store Web Page Lib end...\n";

    std::cout << "[INFO] : Store Web Page Offset Lib begin...\n";
    std::cout << "\t-> " << _offset_lib_path << "\n";
    for (auto it = _offset_lib.begin(); it != _offset_lib.end(); ++it)
    {
        ofs_offset_lib << it->first << " "
                       << it->second.first << " "
                       << it->second.second << "\n";
    }
    std::cout << "[INFO] : Store Web Page Offset Lib end...\n";
}

void PageLib::processWebPage(const std::string &file_path)
{
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(file_path.c_str()) != tinyxml2::XML_SUCCESS)
    {
        std::cerr << "Failed to load XML file: " << file_path << std::endl;
        return;
    }

    auto rss = doc.FirstChildElement("rss");
    if (!rss)
        return;

    auto channel = rss->FirstChildElement("channel");
    if (!channel)
        return;

    for (auto item = channel->FirstChildElement("item"); item; item = item->NextSiblingElement("item"))
    {
        std::string title, link, content;
        getRssItem(item, title, link, content);
        deleteHTMLTag(content);

        if (title != "NULL" && link != "NULL" && content != "NULL" && !content.empty())
        {
            cleanTitle(title);
            cleanContent(content);
            _web_pages.push_back(generateWebPage(title, link, content));
        }
    }
}

void PageLib::getRssItem(tinyxml2::XMLElement *item_element, std::string &title, std::string &link, std::string &content)
{
    auto getElementText = [](tinyxml2::XMLElement *elem)
    {
        return elem ? elem->GetText() : "NULL";
    };

    title = getElementText(item_element->FirstChildElement("title"));
    link = getElementText(item_element->FirstChildElement("link"));
    content = getElementText(item_element->FirstChildElement("description"));

    if (content == "NULL")
    {
        content = getElementText(item_element->FirstChildElement("content"));
    }

    if (link.find("http") == std::string::npos)
    {
        link = "NULL";
    }

    if (content.empty())
    {
        content = "NULL";
    }
}

void PageLib::deleteHTMLTag(std::string &content)
{
    static const std::regex html_tag_pattern("<[^>]*>");
    static const std::regex space_pattern(R"(^\s*)");

    content = std::regex_replace(content, html_tag_pattern, "");
    content = std::regex_replace(content, space_pattern, "");

    if (content.empty())
        content = "NULL";
}

void PageLib::cleanTitle(std::string &title)
{
    size_t error_title_begin_pos = title.find("&$<br>&$");
    if (error_title_begin_pos != std::string::npos)
    {
        title.erase(error_title_begin_pos, 8);
    }

    size_t left_arrow = title.find("<");
    if (left_arrow != std::string::npos)
    {
        title.erase(left_arrow, 1);
        size_t right_arrow = title.find(">");
        if (right_arrow == std::string::npos)
        {
            title = "NULL";
        }
        else
        {
            title.erase(right_arrow, 1);
        }
    }
}

void PageLib::cleanContent(std::string &content)
{
    static const std::regex and_nbps(R"(\&nbsp;)");
    static const std::regex space_pattern(R"(^\s*)");

    content = std::regex_replace(content, and_nbps, "");
    content = std::regex_replace(content, space_pattern, "");
}

std::string PageLib::generateWebPage(const std::string &title, const std::string &link, const std::string &content)
{
    std::ostringstream oss;
    oss << "<webpage>\n"
        << "\t<docid>" << _web_pages.size() + 1 << "</docid>\n"
        << "\t<title>" << title << "</title>\n"
        << "\t<link>" << link << "</link>\n"
        << "\t<content>" << content << "</content>\n"
        << "</webpage>\n";
    return oss.str();
}
