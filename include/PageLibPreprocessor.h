#ifndef PAGE_LIB_PREPROCESSOR_H
#define PAGE_LIB_PREPROCESSOR_H

#include "Configuration.h"
#include "SplitTool.h"
#include "SplitToolCppJieba.h"
#include "WebPage.h"
#include "PageLib.h"
#include "simhash/Simhasher.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <utility>
#include <set>
#include <memory>

class PageLibPreprocessor
{
public:
    using TF_DF_MAP = std::unordered_map<std::string, std::vector<std::pair<int, int>>>;
    using WEIGHT_VES = std::vector<std::vector<std::pair<std::string, double>>>;

    PageLibPreprocessor();
    // ~PageLibPreprocessor() = default;

    static void initConfiguration(const std::string &configPath);
    void createInitialWebPageLib();
    void generateUnRepeatedWebPageLib();
    void generateInvertIndexLib();

private:
    void loadOriginalWebPageOffsetLib();
    void unrepeatedHelper();
    bool isDuplicateWebPage(uint64_t cur_page_figure_print);
    void storeIntoWebPageLib(std::ofstream &ofs, WebPage &cur_page, int &new_page_len);
    void storeIntoOffsetLib(std::ofstream &ofs, int doc_id, int &offset, int page_len);

    void loadUnrepeatedWebPageOffsetLib();
    void invertIndexHelper();
    void saveWordTfDf(TF_DF_MAP &tf_df_umap, const std::map<std::string, int> &word_frequence_map, int doc_id);
    void calculateTFIDF(TF_DF_MAP &tf_df_umap, WEIGHT_VES &doc_weight_ves);
    void minMaxScaler(WEIGHT_VES &doc_weight_ves);
    void storeIntoInvertIndexLib(std::ofstream &ofs);

private:
    std::unique_ptr<SplitTool> _p_split_tool;
    std::unique_ptr<simhash::Simhasher> _p_hasher;

    std::unordered_map<int, std::pair<int, int>> _offset_lib;
    std::unordered_map<std::string, std::set<std::pair<int, double>>> _invert_index_lib;

    std::vector<uint64_t> _page_figure_print_ves;

    std::string _original_webpage_lib_path;
    std::string _original_offset_lib_path;
    std::string _unrepeated_webpage_lib_path;
    std::string _unrepeated_offset_lib_path;
    std::string _inverted_index_lib_path;
};

#endif // PAGE_LIB_PREPROCESSOR_H