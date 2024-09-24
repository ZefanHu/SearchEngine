#ifndef SPLIT_TOOL_CPP_JIEBA_H
#define SPLIT_TOOL_CPP_JIEBA_H

#include "SplitTool.h"
#include <string>
#include <vector>

namespace cppjieba
{
    class Jieba;
}

class SplitToolCppJieba : public SplitTool
{
public:
    SplitToolCppJieba();
    ~SplitToolCppJieba();

    // 删除拷贝和移动构造函数
    SplitToolCppJieba(const SplitToolCppJieba &) = delete;
    SplitToolCppJieba &operator=(const SplitToolCppJieba &) = delete;
    SplitToolCppJieba(SplitToolCppJieba &&) = delete;
    SplitToolCppJieba &operator=(SplitToolCppJieba &&) = delete;

    std::vector<std::string> cut(const std::string &file_content) override;

private:
    cppjieba::Jieba *_jieba;
};

#endif // SPLIT_TOOL_CPP_JIEBA_H
