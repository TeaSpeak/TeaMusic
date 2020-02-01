#pragma once

#include <MusicPlayer.h>
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

			std::string playback = "${command} -reconnect 1 -reconnect_streamed 1 -reconnect_delay_max 5 -hide_banner -stats -i \"${path}\" -vn -bufsize 512k -ac ${channel_count} -ar 48000 -f s16le -acodec pcm_s16le pipe:1";
			std::string playback_seek = "${command} -reconnect 1 -reconnect_streamed 1 -reconnect_delay_max 5 -hide_banner -ss ${seek_offset} -stats -i \"${path}\" -vn -bufsize 512k -ac ${channel_count} -ar 48000 -f s16le -acodec pcm_s16le pipe:1";
		} commands;
	};

	struct FFMpegData {
		static constexpr int CURRENT_VERSION = 1 ;
		enum Type : uint8_t {
			UNDEFINED,
			REPLAY_FILE
		};

		struct Header {
			int version = 1;
			void(*_free)(void*) = nullptr;      /* default will be free(...); Will be used as well for this struct */
			Type type;
		};

		struct FileReplay : public Header {
			char* file_path;                    /* will be freed via _free */
			char* file_description;             /* will be freed via _free */
		};
	};

    class FFMpegProvider : public music::manager::PlayerProvider {
	    public:
		    static FFMpegProvider* instance;
        public:
            FFMpegProvider(std::shared_ptr<FFMpegProviderConfig>  /* config */);
            virtual ~FFMpegProvider();

            bool initialize();

		    threads::Future<std::shared_ptr<UrlInfo>> query_info(const std::string &string, void *pVoid, void *pVoid1) override;

		    threads::Future<std::shared_ptr<music::MusicPlayer>> createPlayer(const std::string &, void*, void*) override;

            std::vector<std::string> availableFormats() override {
                return av_fmt;
            }

            std::vector<std::string> availableProtocols() override {
                return av_protocol;
            }

            std::vector<std::string> av_protocol;
            std::vector<std::string> av_fmt;

            void* readerBase = nullptr;
		    std::thread readerDispatch;

		    inline std::shared_ptr<FFMpegProviderConfig> configuration() { return this->config; }
    	private:
		    std::shared_ptr<FFMpegProviderConfig> config;
    };
}