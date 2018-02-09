#pragma once

#include <experimental/filesystem>
#include "include/MusicPlayer.h"
#include <sql/SqlQuery.h>

namespace fs = std::experimental::filesystem;
namespace yt {
    struct AudioInfo {
        std::string title;
        std::string description;
        std::string stream_url;

        bool live_stream;
    };

    class YTVManager {
        public:
            explicit YTVManager(sql::SqlManager*);
            ~YTVManager();

            bool setup(){ return true; }

            threads::Future<std::shared_ptr<AudioInfo>> downloadAudio(std::string);
            threads::Future<std::shared_ptr<music::MusicPlayer>> playAudio(const std::string&);
        private:
            fs::path root{};
            sql::SqlManager* sql = nullptr;
            threads::ThreadPool _threads{4};
    };
}