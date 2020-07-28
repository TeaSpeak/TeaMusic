#pragma once

#include "include/teaspeak/MusicPlayer.h"

namespace yt {
    struct AudioInfo {
        std::string title{};
        std::string description{};
        std::string thumbnail{};
        std::string stream_url{};

        bool live_stream{false};
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
			std::string query_url = "${command} -v --no-check-certificate -s --print-json --no-playlist --flat-playlist --get-thumbnail \"${video_url}\"";
		} commands;
	};

    class YTVManager {
        public:
            explicit YTVManager() = default;
            ~YTVManager() = default;

            [[nodiscard]] threads::Future<std::shared_ptr<music::UrlInfo>> resolve_url_info(const std::string&);
            [[nodiscard]] threads::Future<std::shared_ptr<AudioInfo>> resolve_stream_info(const std::string&);
            [[nodiscard]] threads::Future<std::shared_ptr<music::MusicPlayer>> create_stream(const std::string &);

		    [[nodiscard]] std::shared_ptr<YTProviderConfig> configuration() const;
    };
}