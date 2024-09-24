#ifndef DIR_SCANNER_H
#define DIR_SCANNER_H

#include "Configuration.h"
#include <vector>
#include <string>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <iostream>

using std::string;
using std::vector;

class DirScanner
{
public:
    vector<string> &getFiles();

    void traverse(string dir);

private:
    vector<string> _files;
};

#endif // DIR_SCANNER_H