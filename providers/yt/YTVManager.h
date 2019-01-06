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

	struct YTProviderConfig {
		std::string youtubedl_command = "youtube-dl";

		struct {
			std::string version = "${command} --version";
			std::string query_video = "${command} -v --no-check-certificate -s --print-json --get-thumbnail \"${video_url}\"";
			/*
			 * --no-playlist => Query video when a playlist and a video is present
			 * --yes-playlist => Query playlist when a playlist and a video is present
			 */
			std::string query_url = "${command} -v -s --print-json --no-playlist --flat-playlist \"${video_url}\"";
		} commands;
	};

    class YTVManager {
        public:
            explicit YTVManager(const std::shared_ptr<YTProviderConfig>& /* config */);
            ~YTVManager();

            bool setup(){ return true; }

		    threads::Future<std::shared_ptr<music::UrlInfo>> resolve_url_info(std::string);
            threads::Future<std::shared_ptr<AudioInfo>> resolve_stream_info(std::string);
            threads::Future<std::shared_ptr<music::MusicPlayer>> create_stream(const std::string &);

		    inline std::shared_ptr<YTProviderConfig> configuration() { return this->config; }
        private:
            threads::ThreadPool _threads{2, "YT Download"};
            std::shared_ptr<YTProviderConfig> config;
    };

}