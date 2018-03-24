#pragma once

#include <include/MusicPlayer.h>
#include <event.h>

extern "C" {
    std::shared_ptr<music::manager::PlayerProvider> EXPORT create_provider();
}

namespace music {
    class FFMpegProvider : public music::manager::PlayerProvider {
	    public:
		    static FFMpegProvider* instance;
        public:
            FFMpegProvider();
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
    };
}