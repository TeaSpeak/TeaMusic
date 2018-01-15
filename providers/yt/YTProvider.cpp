#include "YTProvider.h"
#include "YoutubeMusicPlayer.h"

yt::YTVManager* manager = nullptr;
class YTProvider : public music::manager::PlayerProvider {
    public:
        YTProvider() {
            this->typeName = "";
            this->providerName = "YouTube";
            this->providerDescription = "Playback yt videos";
        }
        threads::Future<std::shared_ptr<music::MusicPlayer>> createPlayer(const std::string &string) override {
            return manager->playAudio(string);
        }

        bool acceptString(const std::string &str) override {
            return str.find("http://") != std::string::npos || str.find("https://") != std::string::npos;
        }

        bool acceptType(const std::string &type) override {
            return type == "yt" || type == "yt-dl";
        }
};

std::shared_ptr<music::manager::PlayerProvider> create_provider() {
    manager = new yt::YTVManager(nullptr); //TODO generate sql db!
    return std::shared_ptr<YTProvider>(new YTProvider(), [](YTProvider* provider){
        if(!provider) return;
        delete provider;
        delete manager;
        manager = nullptr;
    });
}