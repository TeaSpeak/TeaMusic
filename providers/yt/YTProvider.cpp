#include <StringVariable.h>
#include <regex>

#include "./YoutubeMusicPlayer.h"
#include "./YTVManager.h"

#include "providers/shared/INIParser.h"
#include "providers/shared/CommandWrapper.h"

using namespace std;
using namespace music::manager;

yt::YTVManager* manager = nullptr;
extern std::map<std::string, std::unique_ptr<std::regex>>* supported_urls();

class YTProvider : public PlayerProvider {
    public:
        YTProvider() {
            this->providerName = "YouTube";
            this->providerDescription = "Playback yt videos";
        }

        virtual ~YTProvider() = default;

		threads::Future<shared_ptr<music::UrlInfo>> query_info(const std::string &url, void *pVoid, void *pVoid1) override {
			return manager->resolve_url_info(url);
		}

		threads::Future<std::shared_ptr<music::MusicPlayer>> createPlayer(const std::string &string, void*, void*) override {
            return manager->create_stream(string);
        }

        bool acceptString(const std::string &str) override {
        	{
        	    lock_guard lock(this->cache_lock);

		        for(const auto& entry : support_cache)
			        if(entry.first == str) return entry.second;
            }

            bool result = false;
        	auto& map = *supported_urls();

            for(const auto& entry : map)
                if(std::regex_match(str, *entry.second)) {
                	result = true;
                	break;
                }

	        {
		        lock_guard lock(this->cache_lock);

		        support_cache.emplace_back(str, result);
		        while(this->support_cache.size() > 50) this->support_cache.pop_front();
	        }
            return result;
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

	private:
		std::mutex cache_lock;
		std::deque<std::pair<std::string, bool>> support_cache;

};

extern "C"
std::shared_ptr<music::manager::PlayerProvider> EXPORT create_provider() {
    std::string error{};

	if(!libevent::resolve_functions(error)) {
        music::log::log(music::log::err, "[YT-DL] libevent init failed: " + error);
        return nullptr;
	}

    if(!cw::initialize("[YT-DL]", error)) {
        music::log::log(music::log::err, "[YT-DL] " + error);
        return nullptr;
    }

    manager = new yt::YTVManager();

    /* We're not doing a "yt working" check */
#if 0
    {
        std::mutex lock{};
        std::condition_variable lock_cv{};

        bool success{false};

        auto config = manager->configuration();
        auto command = strvar::transform(config->commands.version, strvar::StringValue{"command", config->youtubedl_command});
        cw::execute(command, [&](const cw::Result& result){
            if(!result.full_stderr.empty()) {
                music::log::log(music::log::err, "[YT-DL] youtube-dl versions command failed: " + result.full_stderr_buffer);
                music::log::log(music::log::err, "[YT-DL] How to download/install youtube-dl: https://github.com/rg3/youtube-dl/blob/master/README.md#installation");
            } else if(result.exit_code != 0) {
                music::log::log(music::log::err, "[YT-DL] youtube-dl versions command failed to to non zero exit code (" + std::to_string(result.exit_code) + ")");
                music::log::log(music::log::err, "[YT-DL] How to download/install youtube-dl: https://github.com/rg3/youtube-dl/blob/master/README.md#installation");
            } else if(result.full_output.empty()) {
                music::log::log(music::log::err, "[YT-DL] youtube-dl versions command failed to to an empty response.");
                music::log::log(music::log::err, "[YT-DL] How to download/install youtube-dl: https://github.com/rg3/youtube-dl/blob/master/README.md#installation");
            } else {
                music::log::log(music::log::info, "[YT-DL] Resolved youtube-dl with version " + std::string{result.full_output.back()});
                success = true;
            }

            lock_cv.notify_all();
        }, [&](const std::string& error) {
            music::log::log(music::log::err, "[YT-DL] Versions command timeouted. Failed to initialize provider.");
            lock_cv.notify_all();
        });

        std::unique_lock llock{lock};
        if(lock_cv.wait_for(llock, std::chrono::seconds{20}) == cv_status::timeout) {
            music::log::log(music::log::err, "[YT-DL] Versions command timeouted.");
            success = false;
        }

        if(!success) {
            music::log::log(music::log::err, "[YT-DL] Failed to initialize provider.");
            return nullptr;
        }
    }
#endif

    auto thread_cr = std::thread([] { /* compile regex patterns (async) */
	    music::log::log(music::log::info, "[YT-DL] Compiling patterns");
	    auto begin = chrono::system_clock::now();
	    supported_urls();
	    auto end = chrono::system_clock::now();
	    music::log::log(music::log::info, "[YT-DL] Patterns compiled (" + to_string(chrono::duration_cast<chrono::milliseconds>(end - begin).count()) + "ms)");
    });

    return std::shared_ptr<YTProvider>(new YTProvider(), [thread_cr = std::move(thread_cr)](YTProvider* provider) mutable {
        thread_cr.join();
        if(!provider) return;

        delete provider;
        delete manager;
        manager = nullptr;
    });
}