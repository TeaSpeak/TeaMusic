#include <providers/yt/YoutubeMusicPlayer.h>
#include <providers/shared/pstream.h>
#include <StringVariable.h>
#include <json/json.h>
#include "YTVManager.h"

using namespace std;
using namespace yt;
using namespace music;

YTVManager::YTVManager(const std::shared_ptr<YTProviderConfig>& cfg) : config(cfg) { }

YTVManager::~YTVManager() {}

static const char* audio_prefer_codec_queue[] = {"opus", "vorbis", "mp4a.40.2", "none", ""};

struct FMTInfo {
    string codec;
    int bitrate;
    string url;
};

#define YTDL_DEBUG_PREFIX "[debug] "
inline void filter_debug(vector<string>& lines) {
	bool debug_notified = false;
	for(int index = 0; index < lines.size(); index++) {
		if(lines[index].find(YTDL_DEBUG_PREFIX) == 0) {
			if(!debug_notified) {
				debug_notified = true;
				log::log(log::trace, "[YT-DL] Got command execution debug:");
			}
			log::log(log::trace, "[YT-DL] " + lines[index]);
			lines.erase(lines.begin() + index);
			index--;
		}
	}
}

threads::Future<std::shared_ptr<AudioInfo>> YTVManager::resolve_stream_info(std::string video) {
    threads::Future<std::shared_ptr<AudioInfo>> future;

    auto config = this->config;
    _threads.execute([config, future, video](){
	    redi::pstream proc;
	    {
	    	//video_url
		    //command
		    auto command = strvar::transform(config->commands.query_video,
		    		strvar::StringValue{"command", config->youtubedl_command},
                    strvar::StringValue{"video_url", video}
            );
		    music::log::log(music::log::debug, "[YT-DL] Executing video query command \"" + command + "\"");
		    proc.open(command, redi::pstreams::pstderr | redi::pstreams::pstdout);
	    }

        string response;
        string err;
        size_t bufferLength = 512;
        char buffer[bufferLength];
        string part;
        while(!proc.rdbuf()->exited()) {
            while(proc.out().rdbuf()->in_avail() > 0){
                auto read = proc.out().readsome(buffer, bufferLength);
                if(read > 0) response += string(buffer, read);
            }

            while(proc.err().rdbuf()->in_avail() > 0){
                auto read = proc.err().readsome(buffer, bufferLength);
                if(read > 0) err += string(buffer, read);
            }
	        usleep(10);
        }

	    /* Parsing the response */
        vector<string> available_lines;
	    {   //Parse the lines
		    size_t index = 0;
		    do {
			    auto found = response.find('\n', index);
			    available_lines.push_back(response.substr(index, found - index));
			    if(available_lines.back().find_first_not_of(" \n\r") == std::string::npos) available_lines.pop_back();
			    index = found + 1;
		    } while(index != 0);
	    }

	    vector<string> available_error_lines;
	    {   //Parse the lines
		    size_t index = 0;
		    do {
			    auto found = err.find('\n', index);
			    available_error_lines.push_back(err.substr(index, found - index));
			    if(available_error_lines.back().find_first_not_of(" \n\r") == std::string::npos) available_error_lines.pop_back();
			    index = found + 1;
		    } while(index != 0);
	    }

	    /* Analyzing the response */
	    filter_debug(available_error_lines);
	    filter_debug(available_lines);

	    for(const auto& error : available_error_lines)
		    if(error.find("ERROR") != std::string::npos) {
				future.executionFailed(error);
			    return;
		    }

	    if(available_lines.size() < 2) {
		    log::log(log::err, "[YT-DL] Malformed response (response to small!)");
		    log::log(log::debug, "[YT-DL] Response:");
		    for(const auto& entry : available_lines)
			    log::log(log::debug, "[YT-DL] " + entry);
		    future.executionFailed("Malformed response (to small)");
		    return;
	    }
        log::log(log::trace, "[YT-DL] Got thumbnail response: " + available_lines[available_lines.size() - 2]);
	    log::log(log::trace, "[YT-DL] Got json response: " + available_lines[available_lines.size() - 1]);
        auto thumbnail = available_lines[available_lines.size() - 2];

        Json::Value root;
        Json::CharReaderBuilder rbuilder;
        std::string errs;

        istringstream jsonStream(available_lines[available_lines.size() - 1]);
        bool parsingSuccessful = Json::parseFromStream(rbuilder, jsonStream, &root, &errs);
        if (!parsingSuccessful)
        {
            future.executionFailed("Failed to parse yt json response. (" + errs + ")");
            return;
        }

        auto stream = !root["is_live"].isNull() && root["is_live"].asBool();
        log::log(log::debug, "[YT-DL] Song title: " + root["fulltitle"].asString());
        log::log(log::debug, "[YT-DL] Song id: " + root["id"].asString());
        log::log(log::debug, string() + "[YT-DL] Live stream: " + (stream ? "yes" : "no"));
        //is_live

        auto requests = root["formats"];
        log::log(log::debug, "Request count: " + to_string(requests.size()));

        vector<FMTInfo> urls;
        for (auto request : requests) {
            auto fmt = request["format"].asString();
            int rate = request["abr"].asInt();

            if(stream) {
                if(fmt.find("HLS") == std::string::npos) continue;
            } else {
                if(fmt.find("audio only") == std::string::npos) continue;
            }
            urls.push_back(FMTInfo{request["acodec"].asString(), rate, request["url"].asString()});
        }
        if(urls.empty()) {
            future.executionFailed("Failed to get a valid audio stream");
            return;
        }

        int index = -1;
        int abr = -1; //Audio bitrate
        string streamUrl;
        for(const auto& entry : urls) {
            int i = 0;
            while(audio_prefer_codec_queue[i]) {
                if(entry.codec == audio_prefer_codec_queue[i])
                    break;
                i++;
            }
            if(i == sizeof(audio_prefer_codec_queue) / sizeof(*audio_prefer_codec_queue)) {
                log::log(log::err, "[YT-DL] Could not resolve yt audio quality '" + entry.codec + "'");
                i = -2;
            }

            bool use = false;
            use |= index == -1 || abr == -1;
            if(!use) use |= i < index && index != -2;
            if(!use) use |= abr < entry.bitrate && entry.bitrate != 0;
            if(use) {
                index = i;
                abr = entry.bitrate;
                streamUrl = entry.url;

            }
        }
        if(streamUrl.empty()) {
            log::log(log::err, "[YT-DL] Failed to get a valid audio stream with valid quality!");
            streamUrl = urls[0].url;
        }
        log::log(log::debug, string() + "[YT-DL] Using audio quality " + audio_prefer_codec_queue[index]);
        future.executionSucceed(std::make_shared<AudioInfo>(AudioInfo{root["fulltitle"].asString(), "unknown", thumbnail, streamUrl, stream}));
    });
    return future;
}

threads::Future<std::shared_ptr<music::MusicPlayer>> YTVManager::create_stream(const std::string &video) {
    threads::Future<std::shared_ptr<music::MusicPlayer>> future;

    auto fut = resolve_stream_info(video);
    fut.waitAndGetLater([future, fut](std::shared_ptr<AudioInfo> audio){
        if(fut.succeeded() && audio) return future.executionSucceed(make_shared<music::player::YoutubeMusicPlayer>(audio));
        else return future.executionFailed(fut.errorMegssage());
    }, nullptr);

    return future;
}
