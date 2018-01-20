#include <misc/pstream.h>
#include "YTProvider.h"
#include "YoutubeMusicPlayer.h"

using namespace std;

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
    //youtube-dl --version
    redi::pstream proc;
    proc.open("youtube-dl --version", redi::pstreams::pstdout | redi::pstreams::pstderr | redi::pstreams::pstdin);
    string json;
    string err;
    size_t bufferLength = 512;
    char buffer[bufferLength];
    string part;
    while(!proc.eof()) {
        usleep(10);
        if(proc.out().rdbuf()->in_avail()){
            auto read = proc.out().readsome(buffer, bufferLength);
            if(read > 0) json += string(buffer, read);
        }

        if(proc.err().rdbuf()->in_avail()){
            auto read = proc.err().readsome(buffer, bufferLength);
            if(read > 0) err += string(buffer, read);
        }
    }
    if(err.find('\n') == err.length() - 1) err = err.substr(0, err.length() - 1);
    if(!err.empty()) {
        music::log::log(music::log::err, "[YT-DL] Could not find youtube-dl (Error: \"" + err + "\")");
        music::log::log(music::log::err, "[YT-DL] How to download/install youtube-dl: https://github.com/rg3/youtube-dl/blob/master/README.md#installation");
        return nullptr;
    }
    music::log::log(music::log::info, "[YT-DL] Resolved youtube-dl with version " + json);

    manager = new yt::YTVManager(nullptr); //TODO generate sql db!
    return std::shared_ptr<YTProvider>(new YTProvider(), [](YTProvider* provider){
        if(!provider) return;
        delete provider;
        delete manager;
        manager = nullptr;
    });
}