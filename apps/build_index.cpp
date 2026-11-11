// 离线建库入口：build_index [配置文件路径]
#include <cstdlib>
#include <exception>
#include <filesystem>

#include "search/common/Config.h"
#include "search/common/Log.h"
#include "search/offline/IndexPipeline.h"

int main(int argc, char** argv) {
    namespace log = search::log;
    try {
        const std::filesystem::path config_path = argc > 1 ? argv[1] : "conf/search.conf";
        const search::Config config = search::Config::load(config_path);

        const search::BuildStats stats = search::IndexPipeline(config).run();

        log::info("build finished in ", stats.seconds, "s");
        log::info("  documents read      : ", stats.documents_read);
        log::info("  dropped (empty)     : ", stats.documents_empty);
        log::info("  dropped (duplicate) : ", stats.documents_duplicate);
        log::info("  documents indexed   : ", stats.documents_indexed);
        log::info("  terms / postings    : ", stats.terms, " / ", stats.postings);
        log::info("  suggest dictionary  : ", stats.suggest_words, " words");
        log::info("  output directory    : ", config.index_dir.string());
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        // 原 main 在出错时依然 return 0，脚本/CI 无法感知建库失败
        log::error("build failed: ", error.what());
        return EXIT_FAILURE;
    }
}
