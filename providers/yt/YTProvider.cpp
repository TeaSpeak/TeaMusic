#include <experimental/filesystem>
#include <providers/shared/pstream.h>
#include <StringVariable.h>
#include "providers/shared/INIParser.h"
#include "YoutubeMusicPlayer.h"
#include "YTProvider.h"
#include "YTVManager.h"
#include <regex>

using namespace std;
using namespace music::manager;
namespace fs = std::experimental::filesystem;

yt::YTVManager* manager = nullptr;
extern std::map<std::string, std::unique_ptr<std::regex>>* supported_urls();

class YTProvider : public PlayerProvider {
    public:
        YTProvider() {
            this->providerName = "YouTube";
            this->providerDescription = "Playback yt videos";
        }

        virtual ~YTProvider() = default;

        threads::Future<std::shared_ptr<music::MusicPlayer>> createPlayer(const std::string &string) override {
            return manager->create_stream(string);
        }

//
        bool acceptString(const std::string &str) override {
        	auto& map = *supported_urls();
            for(const auto& entry : map)
                if(std::regex_match(str, *entry.second))
                	return true;
            return false;
        }

        vector<string> availableFormats() override {
            return {};
        }

        vector<string> availableProtocols() override {
            return {"http", "https"};
        }

        size_t weight(const std::string &url) override {
            return this->acceptString(url) ? 100 : 0;
        }
};

std::shared_ptr<music::manager::PlayerProvider> create_provider() {
	std::shared_ptr<yt::YTProviderConfig> config = make_shared<yt::YTProviderConfig>();

	{
		auto config_path = fs::u8path("providers/config_youtube.ini");
		music::log::log(music::log::debug, "[YT-DL] Using config file located at " + config_path.string());
		if(fs::exists(config_path)) {
			INIReader ini_reader(config_path.string());

			if(ini_reader.ParseError()) {
				music::log::log(music::log::err, "[YT-DL] Could not parse config! Using default values");
			} else {
				config->youtubedl_command = ini_reader.Get("general", "youtubedl_command", config->youtubedl_command);
				config->commands.version = ini_reader.Get("commands", "version", config->commands.version);
				config->commands.query_video = ini_reader.Get("commands", "query_video", config->commands.query_video);
				music::log::log(music::log::info, "[YT-DL] Config successfully loaded");
			}
		} else {
			music::log::log(music::log::debug, "[YT-DL] Missing configuration file. Using default values");
		}
	}

    redi::pstream proc;
	{
		auto command = strvar::transform(config->commands.version, strvar::StringValue{"command", config->youtubedl_command});
		music::log::log(music::log::debug, "[YT-DL] Executing versions command \"" + command + "\"");
		proc.open(command, redi::pstreams::pstderr | redi::pstreams::pstdout);
	}
    string json;
    string err;
    size_t bufferLength = 512;
    char buffer[bufferLength];
    string part;
    while(!proc.rdbuf()->exited()) {
        usleep(10);
        if(proc.out().rdbuf()->in_avail() > 0){
            auto read = proc.out().readsome(buffer, bufferLength);
            if(read > 0) json += string(buffer, read);
        }

        if(proc.err().rdbuf()->in_avail() > 0){
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

    while(!json.empty() && (json.back() == '\n' || json.back() == '\r')) json.pop_back();
    music::log::log(music::log::info, "[YT-DL] Resolved youtube-dl with version " + json);

    manager = new yt::YTVManager(config);
    return std::shared_ptr<YTProvider>(new YTProvider(), [](YTProvider* provider){
        if(!provider) return;
        delete provider;
        delete manager;
        manager = nullptr;
    });
}