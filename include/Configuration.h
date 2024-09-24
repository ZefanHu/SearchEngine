#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

using std::string;
using std::unordered_map;
using std::unordered_set;

class Configuration
{
public:
    // 获取单例实例，并初始化配置文件路径
    // 第一次调用时需要提供配置文件路径
    static Configuration &getInstance(const string &filepath = "");

    // 禁用拷贝和移动构造函数及赋值操作
    Configuration(const Configuration &) = delete;
    Configuration &operator=(const Configuration &) = delete;
    Configuration(Configuration &&) = delete;
    Configuration &operator=(Configuration &&) = delete;

    // 获取单个配置项
    string getConfig(const string &key) const;

    // 获取整个配置映射
    const unordered_map<string, string> &getConfigMap() const;

    // 获取停用词集合
    const unordered_set<string> &getStopWordSet() const;

private:
    // 私有构造函数，防止外部实例化
    Configuration(const string &filepath);

    // 加载配置文件
    void loadConfigurationFile();

    // 加载停用词文件
    void loadStopWordFile();

    // 对字符串进行修剪，去除首尾空白字符
    static string trim(const string &str);

    // 配置文件路径
    string _filepath;

    // 配置键值对
    unordered_map<string, string> _configMap;

    // 停用词集合
    unordered_set<string> _stopWords;

    // 单例实例
    static std::unique_ptr<Configuration> _instance;

    // 保证线程安全的初始化
    static std::once_flag _initFlag;
};

#endif // CONFIGURATION_H
