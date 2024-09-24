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

#ifdef TEST_DIR_SCANNER
int main()
{
    try
    {
        // 初始化 Configuration，提供配置文件路径
        Configuration::getInstance("../conf/myconf.conf");

        DirScanner scanner;
        const Configuration &config = Configuration::getInstance();
        string web_page_dir = config.getConfig("web_page_dir");

        scanner.traverse(web_page_dir);

        const auto &files = scanner.getFiles();
        for (const auto &file : files)
        {
            std::cout << file << "\n";
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
#endif // TEST_DIR_SCANNER
