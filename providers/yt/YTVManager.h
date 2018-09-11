#pragma once

#include <ThreadPool/ThreadPool.h>
#include "include/MusicPlayer.h"

namespace yt {
    struct AudioInfo {
        std::string title;
        std::string description;
        std::string thumbnail;
        std::string stream_url;

        bool live_stream;
    };

    class YTVManager {
        public:
            explicit YTVManager();
            ~YTVManager();

            bool setup(){ return true; }

            threads::Future<std::shared_ptr<AudioInfo>> resolve_stream_info(std::string);
            threads::Future<std::shared_ptr<music::MusicPlayer>> create_stream(const std::string &);
        private:
            threads::ThreadPool _threads{2, "YT Download"};
    };

}