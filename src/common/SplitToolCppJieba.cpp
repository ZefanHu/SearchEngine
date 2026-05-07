#include "SplitToolCppJieba.h"
#include "Configuration.h"
#include "simhash/cppjieba/Jieba.hpp"

SplitToolCppJieba::SplitToolCppJieba()
{
    // const Configuration &config = Configuration::getInstance();
    const Configuration &config = Configuration::getInstance("../conf/myconf.conf");
    std::string DICT_PATH = config.getConfig("DICT_PATH");
    std::string HMM_PATH = config.getConfig("HMM_PATH");
    std::string USER_DICT_PATH = config.getConfig("USER_DICT_PATH");
    std::string IDF_PATH = config.getConfig("IDF_PATH");
    std::string STOP_WORD_PATH = config.getConfig("STOP_WORD_PATH");

    _jieba.reset(new cppjieba::Jieba(DICT_PATH, HMM_PATH, USER_DICT_PATH, IDF_PATH, STOP_WORD_PATH));
}

SplitToolCppJieba::~SplitToolCppJieba() = default;

std::vector<std::string> SplitToolCppJieba::cut(const std::string &file_content)
{
    std::vector<std::string> ret;
    _jieba->Cut(file_content, ret);
    return ret;
}
