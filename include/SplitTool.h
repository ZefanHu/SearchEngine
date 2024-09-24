#ifndef SPLIT_TOOL_H
#define SPLIT_TOOL_H

#include <vector>
#include <string>

class SplitTool
{
public:
    virtual std::vector<std::string> cut(const std::string &file_content) = 0;
};

#endif // SPLIT_TOOL_H