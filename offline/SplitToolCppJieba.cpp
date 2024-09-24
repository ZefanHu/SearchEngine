#include "SplitToolCppJieba.h"
#include "Configuration.h"
#include "simhash/cppjieba/Jieba.hpp"

SplitToolCppJieba::SplitToolCppJieba()
    : _jieba(nullptr)
{
    const Configuration &config = Configuration::getInstance();
    std::string DICT_PATH = config.getConfig("DICT_PATH");
    std::string HMM_PATH = config.getConfig("HMM_PATH");
    std::string USER_DICT_PATH = config.getConfig("USER_DICT_PATH");
    std::string IDF_PATH = config.getConfig("IDF_PATH");
    std::string STOP_WORD_PATH = config.getConfig("STOP_WORD_PATH");

    _jieba = new cppjieba::Jieba(DICT_PATH, HMM_PATH, USER_DICT_PATH, IDF_PATH, STOP_WORD_PATH);
}

SplitToolCppJieba::~SplitToolCppJieba()
{
    delete _jieba;
}

std::vector<std::string> SplitToolCppJieba::cut(const std::string &file_content)
{
    std::vector<std::string> ret;
    _jieba->Cut(file_content, ret);
    return ret;
}

#ifdef TEST_SPLIT_TOOL_CPP_JIEBA
int main(void)
{
    try
    {
        // 先初始化 Configuration
        Configuration::getInstance("../conf/myconf.conf");

        SplitToolCppJieba jb;
        std::string s = "感觉细分的赛道越来越多了";
        std::vector<std::string> res = jb.cut(s);
        for (const auto &str : res)
        {
            std::cout << str << "\n";
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
#endif // TEST_SPLIT_TOOL_CPP_JIEBA
