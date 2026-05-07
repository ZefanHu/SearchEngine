#include "../include/PageLibPreprocessor.h"
#include <iostream>

int main()
{
    try
    {
        PageLibPreprocessor::initConfiguration("../conf/myconf.conf");

        PageLibPreprocessor plp;
        plp.createInitialWebPageLib();
        plp.generateUnRepeatedWebPageLib();
        plp.generateInvertIndexLib();

        std::cout << "Offline index generated successfully.\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
    }
    return 0;
}
