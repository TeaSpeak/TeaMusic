#pragma once

#include <include/MusicPlayer.h>
#include <event.h>
#include <string>

extern "C" {
    std::shared_ptr<music::manager::PlayerProvider> EXPORT create_provider();
}

namespace music {
	struct FFMpegProviderConfig {
		std::string ffmpeg_command = "ffmpeg";

		struct {
			std::string version = "${command} -version";
			std::string formats = "${command} -formats";
			std::string protocols = "${command} -protocols";

			std::string playback = "${command} -hide_banner -stats -i \"${path}\" -vn -bufsize 512k -ac ${channel_count} -ar 48000 -f s16le -acodec pcm_s16le pipe:1";
			std::string playback_seek = "${command} -hide_banner -ss ${seek_offset} -stats -i \"${path}\" -vn -bufsize 512k -ac ${channel_count} -ar 48000 -f s16le -acodec pcm_s16le pipe:1";
		} commands;
	};

    class FFMpegProvider : public music::manager::PlayerProvider {
	    public:
		    static FFMpegProvider* instance;
        public:
            FFMpegProvider(const std::shared_ptr<FFMpegProviderConfig>& /* config */);
            virtual ~FFMpegProvider();

            threads::Future<std::shared_ptr<music::MusicPlayer>> createPlayer(const std::string &string) override;

            std::vector<std::string> availableFormats() override {
                return av_fmt;
            }

            std::vector<std::string> availableProtocols() override {
                return av_protocol;
            }

            std::vector<std::string> av_protocol;
            std::vector<std::string> av_fmt;

            event_base* readerBase = nullptr;
		    threads::Thread* readerDispatch = nullptr;

		    inline std::shared_ptr<FFMpegProviderConfig> configuration() { return this->config; }
    	private:
		    std::shared_ptr<FFMpegProviderConfig> config;
    };
}