#pragma once

#include <sql/SqlQuery.h>
#include <experimental/filesystem>
#include "include/MusicPlayer.h"

namespace fs = std::experimental::filesystem;
namespace yt {
    struct AudioInfo {
        std::string title;
        std::string description;
        std::string file;
    };

    class YTVManager {
        public:
            explicit YTVManager(sql::SqlData*);
            ~YTVManager();

            bool setup(){ return true; }

            threads::Future<std::shared_ptr<AudioInfo>> downloadAudio(std::string);
            threads::Future<std::shared_ptr<music::MusicPlayer>> playAudio(const std::string&);
        private:
            fs::path root;
            sql::SqlData* sql;
            threads::ThreadPool _threads;
    };
}