#include "DirScanner.h"
#include "Configuration.h"
#include <stdexcept>

vector<string> &DirScanner::getFiles()
{
    return _files;
}

void DirScanner::traverse(string dir)
{
    DIR *p_dir = opendir(dir.c_str());

    if (p_dir == nullptr)
    {
        throw std::runtime_error("Failed to open directory: " + dir);
    }

    struct dirent *pdirent = nullptr;

    while ((pdirent = readdir(p_dir)) != nullptr)
    {
        if (strcmp(pdirent->d_name, ".") == 0 || strcmp(pdirent->d_name, "..") == 0)
        {
            continue;
        }

        if (pdirent->d_type == DT_REG) // 只处理常规文件
        {
            _files.emplace_back(dir + "/" + pdirent->d_name);
        }
    }

    closedir(p_dir);
}
