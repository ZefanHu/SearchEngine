#include "Configuration.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <algorithm>

using std::cout;
using std::endl;
using std::ifstream;
using std::stringstream;

// 初始化静态成员变量
std::unique_ptr<Configuration> Configuration::_instance = nullptr;
std::once_flag Configuration::_initFlag;

// 获取单例实例
Configuration &Configuration::getInstance(const string &filepath)
{
    std::call_once(_initFlag, [&filepath]()
                   {
        if (filepath.empty()) {
            throw std::runtime_error("Configuration file path must be provided on first call to getInstance.");
        }
        _instance.reset(new Configuration(filepath));
        // 注册析构函数
        atexit([]() { Configuration::_instance.reset(); }); });

    if (!_instance)
    {
        throw std::runtime_error("Configuration instance is not initialized. Provide a filepath on first call.");
    }

    return *_instance;
}

// 私有构造函数
Configuration::Configuration(const string &filepath)
    : _filepath(filepath)
{
    loadConfigurationFile();
    loadStopWordFile();
}

// 静态方法：去除字符串首尾的空白字符
string Configuration::trim(const string &str)
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

// 加载配置文件
void Configuration::loadConfigurationFile()
{
    ifstream ifs(_filepath);
    if (!ifs.is_open())
    {
        cout << "无法打开配置文件: " << _filepath << "\n";
        throw std::runtime_error("Failed to open configuration file.");
    }

    string line;
    while (getline(ifs, line))
    {
        // 跳过空行和注释行
        if (line.empty() || line[0] == '#' || line.find("##") == 0)
        {
            continue;
        }

        // 查找等号的位置
        size_t pos = line.find('=');
        if (pos == string::npos)
        {
            continue; // 无效行，跳过
        }

        // 分割键和值，并去除首尾空白字符
        string key = trim(line.substr(0, pos));
        string value = trim(line.substr(pos + 1));

        if (!key.empty() && !value.empty())
        {
            _configMap[key] = value;
        }
    }

    cout << "[ 配置加载完成 ] 总项数: " << _configMap.size() << "\n";
    // for (const auto &pair : _configMap)
    // {
    //     cout << pair.first << " : " << pair.second << "\n";
    // }
}

// 加载停用词文件
void Configuration::loadStopWordFile()
{
    // 查找停用词文件路径
    auto it_zh = _configMap.find("zh_stop_words_file");
    auto it_en = _configMap.find("en_stop_words_file");

    if (it_zh == _configMap.end() || it_en == _configMap.end())
    {
        cout << "未找到停用词文件路径\n";
        return;
    }

    ifstream zh_ifs(it_zh->second);
    ifstream en_ifs(it_en->second);

    if (!zh_ifs.is_open() || !en_ifs.is_open())
    {
        cout << "无法打开停用词文件: "
             << ((!zh_ifs.is_open()) ? it_zh->second : it_en->second) << "\n";
        return;
    }

    string stop_word;
    while (en_ifs >> stop_word)
    {
        _stopWords.insert(stop_word);
    }

    while (zh_ifs >> stop_word)
    {
        _stopWords.insert(stop_word);
    }

    // cout << "[ 停用词加载完成 ] 总数: " << _stopWords.size() << "\n";
}

// 获取单个配置项
string Configuration::getConfig(const string &key) const
{
    auto it = _configMap.find(key);
    return (it != _configMap.end()) ? it->second : "";
}

// 获取整个配置映射
const unordered_map<string, string> &Configuration::getConfigMap() const
{
    return _configMap;
}

// 获取停用词集合
unordered_set<string> &Configuration::getStopWordSet()
{
    return _stopWords;
}

// 测试用的 main 函数
#ifdef CONFIGURATION_TEST_MAIN
int main()
{
    try
    {
        // 第一次调用需要提供配置文件路径
        Configuration &config = Configuration::getInstance("../conf/myconf.conf");

        // 获取单个配置项
        // string ip = config.getConfig("IP");
        // string port = config.getConfig("PORT");
        // cout << "IP: " << ip << "\n";
        // cout << "PORT: " << port << "\n";

        // 获取整个配置映射
        const auto &cfgMap = config.getConfigMap();
        cout << "\n全部配置项:\n";
        for (const auto &pair : cfgMap)
        {
            cout << pair.first << " : " << pair.second << "\n";
        }

        // 获取停用词集合
        const auto &stopWords = config.getStopWordSet();
        cout << "\n停用词数量: " << stopWords.size() << "\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "配置加载失败: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
#endif // CONFIGURATION_TEST_MAIN
