#include "DictProducer.h"
#include "Configuration.h"
#include "SplitTool.h"
#include <iostream>
#include <fstream>
#include <dirent.h>
#include <algorithm>

using namespace std;

DictProducer::DictProducer(LanguageType language, SplitTool *tool)
    : _language(language), _cut_tool(tool)
{
    const auto &config = Configuration::getInstance();
    string prefix = (_language == LanguageType::English) ? "en_" : "zh_";

    _corpus_dir = config.getConfig(prefix + "corpus_dir" + (_language == LanguageType::Chinese ? "_dir" : ""));
    _stop_words_file = config.getConfig(prefix + "stop_words_file");
    _dict_file = config.getConfig(prefix + "dict.dat");
    _dict_index_file = config.getConfig(prefix + "dict_index.dat");

    readCorpusFiles();
    initStopWordSet();
}

void DictProducer::generateAllFiles()
{
    generateDictFile();
    generateDictIndexFile();
}

void DictProducer::generateDictFile()
{
    (_language == LanguageType::English) ? buildEnglishDict() : buildChineseDict();
    storeDictFile();
}

void DictProducer::generateDictIndexFile()
{
    createDictIndex();
    storeDictIndexFile();
}

void DictProducer::readCorpusFiles()
{
    DIR *dir = opendir(_corpus_dir.c_str());
    if (!dir)
    {
        cerr << "Error opening directory: " << _corpus_dir << endl;
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (entry->d_type == DT_REG)
        {
            _files.push_back(_corpus_dir + "/" + entry->d_name);
        }
    }
    closedir(dir);
}

void DictProducer::initStopWordSet()
{
    ifstream ifs(_stop_words_file);
    if (!ifs)
    {
        cerr << "Error opening stop word file: " << _stop_words_file << endl;
        return;
    }

    string word;
    while (ifs >> word)
    {
        _stop_words.insert(word);
    }
    cout << "Loaded " << _stop_words.size() << " stop words." << endl;
}

bool DictProducer::isStopWord(const string &word) const noexcept
{
    return _stop_words.find(word) != _stop_words.end();
}

void DictProducer::buildEnglishDict()
{
    map<string, int> temp_dict;
    for (const auto &file : _files)
    {
        ifstream ifs(file);
        string word;
        while (ifs >> word)
        {
            extractEnglishWord(word);
            if (!word.empty() && !isStopWord(word))
            {
                temp_dict[word]++;
            }
        }
    }
    saveWord(temp_dict);
}

void DictProducer::buildChineseDict()
{
    map<string, int> temp_dict;
    for (const auto &file : _files)
    {
        ifstream ifs(file);
        string content, temp;
        while (ifs >> temp)
        {
            extractChineseWord(temp);
            content += temp;
        }
        auto words = _cut_tool->cut(content);
        for (const auto &word : words)
        {
            if (!isStopWord(word))
            {
                temp_dict[word]++;
            }
        }
    }
    saveWord(temp_dict);
}

void DictProducer::createDictIndex()
{
    for (size_t i = 0; i < _dict.size(); ++i)
    {
        const auto &word = _dict[i].first;
        size_t ch_num = word.size() / nBytesCode(word[0]);
        for (size_t j = 0, pos = 0; j < ch_num; ++j)
        {
            size_t cur_ch_bytes = nBytesCode(word[pos]);
            _index[word.substr(pos, cur_ch_bytes)].insert(i);
            pos += cur_ch_bytes;
        }
    }
}

void DictProducer::storeDictFile() const
{
    ofstream ofs(_dict_file);
    if (!ofs)
    {
        cerr << "Error creating dictionary file: " << _dict_file << endl;
        return;
    }
    for (const auto &entry : _dict)
    {
        ofs << entry.first << " " << entry.second << "\n";
    }
    cout << "Dictionary stored in: " << _dict_file << endl;
}

void DictProducer::storeDictIndexFile() const
{
    ofstream ofs(_dict_index_file);
    if (!ofs)
    {
        cerr << "Error creating dictionary index file: " << _dict_index_file << endl;
        return;
    }
    for (const auto &entry : _index)
    {
        ofs << entry.first;
        for (int index : entry.second)
        {
            ofs << " " << index;
        }
        ofs << "\n";
    }
    cout << "Dictionary index stored in: " << _dict_index_file << endl;
}

size_t DictProducer::nBytesCode(char ch) noexcept
{
    if (ch & 0x80)
    {
        int bytes = 1;
        for (int i = 0; i < 6; ++i)
        {
            if (ch & (1 << (6 - i)))
                ++bytes;
            else
                break;
        }
        return bytes;
    }
    return 1;
}

void DictProducer::saveWord(map<string, int> &temp_dict)
{
    _dict.reserve(_dict.size() + temp_dict.size());
    for (const auto &entry : temp_dict)
    {
        _dict.emplace_back(entry);
    }
}

void DictProducer::extractEnglishWord(string &word)
{
    word.erase(remove_if(word.begin(), word.end(),
                         [](unsigned char c)
                         { return !isalpha(c); }),
               word.end());
    transform(word.begin(), word.end(), word.begin(), ::tolower);
}

bool DictProducer::isChinese(char ch) noexcept
{
    return (ch & 0x80) != 0;
}

void DictProducer::extractChineseWord(string &word)
{
    string result;
    for (size_t i = 0; i < word.size();)
    {
        size_t bytes = nBytesCode(word[i]);
        if (isChinese(word[i]))
        {
            result.append(word, i, bytes);
        }
        i += bytes;
    }
    word = move(result);
}
#ifdef TEST_DICT_PRODUCER
#include "SplitToolCppJieba.h"

int main()
{
    try
    {
        // 确保 Configuration 已经初始化
        Configuration::getInstance("../conf/myconf.conf");

        // 创建英文字典
        DictProducer englishProducer(LanguageType::English);
        englishProducer.generateAllFiles();
        cout << "English dictionary and index files generated successfully." << endl;

        // 创建中文字典
        SplitToolCppJieba jiebaTool;
        DictProducer chineseProducer(LanguageType::Chinese, &jiebaTool);
        chineseProducer.generateAllFiles();
        cout << "Chinese dictionary and index files generated successfully." << endl;
    }
    catch (const exception &e)
    {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
#endif // TEST_DICT_PRODUCER