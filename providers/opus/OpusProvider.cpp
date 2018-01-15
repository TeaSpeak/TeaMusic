#include "OpusProvider.h"
#include "OpusMusicPlayer.h"

class OpusProvider : public music::manager::PlayerProvider {
    public:
        OpusProvider() {
            this->typeName = "opus";
            this->providerName = "Opus";
            this->providerDescription = "Playback opus music files";
        }
        threads::Future<std::shared_ptr<music::MusicPlayer>> createPlayer(const std::string &string) override {
            auto player = std::make_shared<music::player::OpusMusicPlayer>(string);
            auto future = threads::Future<std::shared_ptr<music::MusicPlayer>>();
            future.executionSucceed(std::dynamic_pointer_cast<music::MusicPlayer>(player));
            return future;
        }
};

std::shared_ptr<music::manager::PlayerProvider> create_provider() {
    return std::make_shared<OpusProvider>();
}