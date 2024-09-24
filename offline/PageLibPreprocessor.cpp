#include "PageLibPreprocessor.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

void PageLibPreprocessor::initConfiguration(const std::string &configPath)
{
    Configuration::getInstance(configPath);
}

PageLibPreprocessor::PageLibPreprocessor()
{
    const auto &config = Configuration::getInstance();

    _original_webpage_lib_path = config.getConfig("webpage.dat");
    _original_offset_lib_path = config.getConfig("offsetlib.dat");
    _unrepeated_webpage_lib_path = config.getConfig("unrepeated_webpage.dat");
    _unrepeated_offset_lib_path = config.getConfig("unrepeated_offsetlib.dat");
    _inverted_index_lib_path = config.getConfig("inverted_index.dat");

    std::string dictPath = config.getConfig("DICT_PATH");
    std::string modelPath = config.getConfig("HMM_PATH");
    std::string idfPath = config.getConfig("IDF_PATH");
    std::string stopWords = config.getConfig("STOP_WORD_PATH");

    _p_split_tool.reset(new SplitToolCppJieba());
    _p_hasher.reset(new simhash::Simhasher(dictPath, modelPath, idfPath, stopWords));
}

void PageLibPreprocessor::createInitialWebPageLib()
{
    PageLib pageLib;
    pageLib.create();
    pageLib.store();
}

// PageLibPreprocessor::~PageLibPreprocessor() = default;

// void PageLibPreprocessor::initConfiguration(const std::string &configPath)
// {
//     Configuration::getInstance(configPath);
// }

void PageLibPreprocessor::generateUnRepeatedWebPageLib()
{
    loadOriginalWebPageOffsetLib();
    unrepeatedHelper();
}

void PageLibPreprocessor::loadOriginalWebPageOffsetLib()
{
    std::cout << "[INFO] : Load Original Web Page Offset Lib...\n";
    std::ifstream offset_lib_ifs(_original_offset_lib_path);

    string line;
    while (std::getline(offset_lib_ifs, line))
    {
        std::istringstream iss(line);
        int doc_id = 0, offset = 0, page_len = 0;
        iss >> doc_id >> offset >> page_len;
        _offset_lib[doc_id] = {offset, page_len};
    }

    std::cout << "[INFO] : Load Original Web Page Offset Lib Success...\n";
}

void PageLibPreprocessor::unrepeatedHelper()
{
    std::cout << "[INFO] : Generate Unrepeated Web Page Lib And Offset Lib...\n";
    std::cout << "[INFO] : Remove Duplicate Web Page...\n";

    std::ifstream web_page_lib_ifs(Configuration::getInstance().getConfig("webpage.dat"));
    std::ofstream unrepeated_web_page_lib_ofs(Configuration::getInstance().getConfig("unrepeated_webpage.dat"));
    std::ofstream unrepeated_offset_lib_ofs(Configuration::getInstance().getConfig("unrepeated_offsetlib.dat"));

    int new_offset = 0;
    for (size_t i = 0; i < _offset_lib.size(); i++)
    {
        double percentage = double(100) * i / _offset_lib.size();
        printf("\t-> %5.2f%% : %ld\r", percentage, i);
        fflush(stdout);

        int doc_id = i + 1;
        int offset = _offset_lib[doc_id].first;
        int page_len = _offset_lib[doc_id].second;

        std::vector<char> buffer(page_len + 1);
        web_page_lib_ifs.seekg(offset);
        web_page_lib_ifs.read(buffer.data(), page_len);
        buffer[page_len] = '\0';

        std::string cur_page_content(buffer.data());

        WebPage cur_page(cur_page_content);

        cur_page.unrepeatedWebPageLibHelper(_p_hasher.get());

        uint64_t cur_page_figure_print = cur_page.getFigurePrint();

        if (isDuplicateWebPage(cur_page_figure_print))
        {
            continue;
        }
        else
        {
            int new_page_len = 0;
            _page_figure_print_ves.push_back(cur_page_figure_print);
            storeIntoWebPageLib(unrepeated_web_page_lib_ofs, cur_page, new_page_len);
            storeIntoOffsetLib(unrepeated_offset_lib_ofs, _page_figure_print_ves.size(), new_offset, new_page_len);
        }
    }

    std::cout << "\t-> 100.00% : " << _offset_lib.size() << " --> " << _page_figure_print_ves.size() << "\n";
    std::cout << "[INFO] : Remove Duplicate Web Page Success...\n";
    std::cout << "[INFO] : Generate Unrepeated Web Page Lib And Offset Lib Success...\n";
}

bool PageLibPreprocessor::isDuplicateWebPage(uint64_t cur_page_figure_print)
{
    for (const auto &print : _page_figure_print_ves)
    {
        if (simhash::Simhasher::isEqual(cur_page_figure_print, print, 5)) // 将 3 改为 5 或更高的值
        {
            return true;
        }
    }
    return false;
}

void PageLibPreprocessor::storeIntoWebPageLib(std::ofstream &ofs, WebPage &cur_page, int &new_page_len)
{
    std::ostringstream oss;
    oss << "<webpage>\n"
        << "\t<docid>" << _page_figure_print_ves.size() << "</docid>\n"
        << "\t<title>" << cur_page.getTitle() << "</title>\n"
        << "\t<link>" << cur_page.getURL() << "</link>\n"
        << "\t<content>" << cur_page.getContent() << "</content>\n"
        << "</webpage>\n";

    ofs << oss.str();
    new_page_len = oss.str().size();
}

void PageLibPreprocessor::storeIntoOffsetLib(std::ofstream &ofs, int doc_id, int &offset, int page_len)
{
    ofs << doc_id << " " << offset << " " << page_len - 1 << "\n";
    offset += page_len;
}

void PageLibPreprocessor::generateInvertIndexLib()
{
    loadUnrepeatedWebPageOffsetLib();
    invertIndexHelper();
}

void PageLibPreprocessor::loadUnrepeatedWebPageOffsetLib()
{
    std::cout << "[INFO] : Load Unrepeated Web Page Offset Lib...\n";
    std::ifstream offset_lib_ifs(_unrepeated_offset_lib_path);

    string line;
    while (std::getline(offset_lib_ifs, line))
    {
        std::istringstream iss(line);
        int doc_id = 0, offset = 0, page_len = 0;
        iss >> doc_id >> offset >> page_len;
        _offset_lib[doc_id] = {offset, page_len};
    }

    std::cout << "[INFO] : Load Unrepeated Web Page Offset Lib Success...\n";
}

void PageLibPreprocessor::invertIndexHelper()
{
    std::cout << "[INFO] : Generate Invert Index Lib...\n";
    std::cout << "[INFO] : Load Unrepeated Web Page Lib...\n";

    std::ifstream unrepeated_web_page_lib_ifs(_unrepeated_webpage_lib_path);
    std::ofstream invert_index_lib_ofs(_inverted_index_lib_path);

    TF_DF_MAP tf_df_umap;
    WEIGHT_VES doc_weight_ves(_offset_lib.size() + 1);

    for (size_t i = 0; i < _offset_lib.size(); ++i)
    {
        double percentage = double(100) * i / _offset_lib.size();
        printf("\t-> %5.2f%% : %ld\r", percentage, i);
        fflush(stdout);

        int doc_id = i + 1;
        int offset = _offset_lib[doc_id].first;
        int page_len = _offset_lib[doc_id].second;

        std::vector<char> buffer(page_len + 1);
        unrepeated_web_page_lib_ifs.seekg(offset);
        unrepeated_web_page_lib_ifs.read(buffer.data(), page_len);

        string cur_page_content(buffer.data());
        WebPage cur_page(cur_page_content);
        cur_page.invertIndexLibHelper(_p_split_tool.get());

        saveWordTfDf(tf_df_umap, cur_page.getWordFrequenceMap(), doc_id);
    }

    std::cout << "\t-> 100.00% : " << _offset_lib.size() << "\n";
    std::cout << "[INFO] : Load Unrepeated Web Page Lib Success...\n";

    std::cout << "[INFO] : Calculate Web Page Word's TF-IDF...\n";
    calculateTFIDF(tf_df_umap, doc_weight_ves);
    std::cout << "\t-> Calculate Success...\n"
              << "\t-> Min-Max-Scaler Web Page Word's TF-IDF\n";

    minMaxScaler(doc_weight_ves);
    std::cout << "\t-> Min-Max-Scaler Web Page Word's TF-IDF Success\n";
    std::cout << "[INFO] : Calculate Web Page Word's TF-IDF Success...\n";

    storeIntoInvertIndexLib(invert_index_lib_ofs);
    std::cout << "[INFO] : Generate Invert Index Lib Success...\n";
}

void PageLibPreprocessor::saveWordTfDf(TF_DF_MAP &tf_df_umap, const std::map<string, int> &word_frequence_map, int doc_id)
{
    for (const auto &it : word_frequence_map)
    {
        tf_df_umap[it.first].emplace_back(doc_id, it.second);
    }
}

void PageLibPreprocessor::calculateTFIDF(TF_DF_MAP &tf_df_umap, WEIGHT_VES &doc_weight_ves)
{
    size_t i = 0;
    size_t N = _offset_lib.size();

    for (const auto &it : tf_df_umap)
    {
        double percentage = double(100) * i / tf_df_umap.size();
        printf("\t-> %5.2f%% : %ld\r", percentage, i++);
        fflush(stdout);

        size_t DF = it.second.size();
        double IDF = std::log2(double(N) / (DF + 1) + 1);

        for (const auto &doc_freq : it.second)
        {
            size_t doc_id = doc_freq.first;
            size_t TF = doc_freq.second;
            double w = TF * IDF;
            doc_weight_ves[doc_id].emplace_back(it.first, w);
        }
    }

    printf("\t-> 100.00%% : %ld\n", tf_df_umap.size());
}

void PageLibPreprocessor::minMaxScaler(WEIGHT_VES &doc_weight_ves)
{
    for (size_t doc_id = 1; doc_id < doc_weight_ves.size(); ++doc_id)
    {
        double sigma_weight_square = 0;

        for (const auto &word_weight : doc_weight_ves[doc_id])
        {
            sigma_weight_square += word_weight.second * word_weight.second;
        }

        double sqrt_sigma = std::sqrt(sigma_weight_square);

        for (auto &word_weight : doc_weight_ves[doc_id])
        {
            word_weight.second /= sqrt_sigma;
            _invert_index_lib[word_weight.first].insert({doc_id, word_weight.second});
        }
    }
}

void PageLibPreprocessor::storeIntoInvertIndexLib(std::ofstream &ofs)
{
    for (const auto &umit : _invert_index_lib)
    {
        ofs << umit.first;
        for (const auto &sit : umit.second)
        {
            ofs << " " << sit.first << " " << sit.second;
        }
        ofs << "\n";
    }
}

#ifdef TEST_PAGE_LIB_PREPROCESSOR
int main()
{
    try
    {
        PageLibPreprocessor::initConfiguration("../conf/myconf.conf");

        PageLibPreprocessor plp;

        // 首先创建初始的网页库
        plp.createInitialWebPageLib();

        // 然后进行去重和倒排索引生成
        plp.generateUnRepeatedWebPageLib();
        plp.generateInvertIndexLib();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
#endif