#ifndef DICT_PRODUCER_H
#define DICT_PRODUCER_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_set>

class SplitTool;

enum class LanguageType
{
    English = 0,
    Chinese
};

class DictProducer final
{
public:
    DictProducer(LanguageType language, SplitTool* tool = nullptr);

    // 删除拷贝和移动构造函数
    DictProducer(const DictProducer&) = delete;
    DictProducer& operator=(const DictProducer&) = delete;
    DictProducer(DictProducer&&) = delete;
    DictProducer& operator=(DictProducer&&) = delete;

    void generateAllFiles();
    void generateDictFile();
    void generateDictIndexFile();

private:
    void readCorpusFiles();
    void initStopWordSet();
    bool isStopWord(const std::string& word) const noexcept;

    void buildEnglishDict();
    void buildChineseDict();
    void createDictIndex();
    void storeDictFile() const;
    void storeDictIndexFile() const;

    static std::size_t nBytesCode(char ch) noexcept;
    void saveWord(std::map<std::string, int>& temp_dict);
    static void extractEnglishWord(std::string& word);
    static bool isChinese(char ch) noexcept;
    static void extractChineseWord(std::string& word);

    LanguageType _language;
    SplitTool* _cut_tool;
    std::vector<std::string> _files;
    std::vector<std::pair<std::string, int>> _dict;
    std::map<std::string, std::set<int>> _index;
    std::unordered_set<std::string> _stop_words;
    std::string _corpus_dir;
    std::string _stop_words_file;
    std::string _dict_file;
    std::string _dict_index_file;
};

#endif // DICT_PRODUCER_H
