#ifndef __DICTIONARY_H__
#define __DICTIONARY_H__

#include "Configuration.h"
#include "SplitToolCppJieba.h"
#include <vector>
#include <set>
#include <map>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <utility>
#include <memory>
#include <mutex>

using std::ifstream;
using std::istringstream;
using std::map;
using std::ofstream;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::unordered_map;
using std::vector;

class Dictionary
{
public:
    static Dictionary &getInstance();

    Dictionary(const Dictionary &) = delete;
    Dictionary &operator=(const Dictionary &) = delete;
    Dictionary(Dictionary &&) = delete;
    Dictionary &operator=(Dictionary &&) = delete;

    vector<pair<string, int>> &getWordFrequenceDict();

    map<string, set<int>> &getWordFequenceIndexMap();

    vector<pair<int, int>> &getOffsetLib();

    unordered_map<string, unordered_map<int, double>> &getInvertIndexLib();

    SplitTool *getSplitTool();

private:
    Dictionary();

    void loadDataSet();
    void loadWebPageFile();

    SplitTool *_p_split_tool = nullptr;

    vector<pair<string, int>> _dict_freq_vec;

    map<string, set<int>> _word_index_map;

    vector<pair<int, int>> _offset_lib_ves;

    unordered_map<string, unordered_map<int, double>> _invert_index_lib;

    static std::unique_ptr<Dictionary> _instance;
    static std::once_flag _initFlag;
};

#endif
