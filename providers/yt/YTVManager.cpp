#include <StringVariable.h>
#include <json/json.h>
#include <memory>
#include <utility>
#include <experimental/filesystem>

#include "providers/shared/INIParser.h"
#include "providers/shared/CommandWrapper.h"

#include "./YTVManager.h"
#include "./YoutubeMusicPlayer.h"

namespace fs = std::experimental::filesystem;

using namespace std;
using namespace yt;
using namespace music;

static const char* audio_prefer_codec_queue[] = {"opus", "vorbis", "mp4a.40.2", "none", ""};

struct FMTInfo {
    string codec;
    int bitrate;
    string url;
};

inline std::vector<std::string_view> remove_debug_messages(const std::vector<std::string_view>& lines) {
    std::vector<std::string_view> result{};
    result.reserve(lines.size());

    for(const auto& line : lines) {
        if(line.starts_with("[debug] "))
            continue;

        result.push_back(line);
    }

    return result;
}

std::shared_ptr<music::UrlInfo> parse_url_info(const cw::Result& result, std::string& error) {
    auto stderr_lines = remove_debug_messages(result.full_stderr);
    auto stdout_lines = remove_debug_messages(result.full_stdout);

    /* Analyzing the response */
    {
        deque<string> warnings;
        for(const auto& line : stderr_lines) {
            if(line.find("ERROR") == std::string::npos)
                continue;

            error = line;
            return nullptr;
        }
    }

    if(stdout_lines.empty()) {
        error = "command response is too small";
        return nullptr;
    }

    std::string thumbnail;
    deque<unique_ptr<Json::Value>> jsons;

    std::string json_parse_error{};
    for(const auto& line : stdout_lines) {
        if(line.empty() || line[0] != '{') {
            if(line.starts_with("https://") == 0 && thumbnail.empty()) {
                thumbnail = line;
                continue;
            }

            log::log(log::trace, "[YT-DL][Query] Invalid query line \"" + std::string{line} + "\". Skip parsing");
            continue;
        }

        auto root = make_unique<Json::Value>();
        Json::CharReaderBuilder rbuilder;
        std::string errs;

        istringstream jsonStream{std::string{line}};
        bool parsingSuccessful = Json::parseFromStream(rbuilder, jsonStream, &*root, &error);
        if (!parsingSuccessful) {
            if(error.empty())
                json_parse_error = error;
            log::log(log::trace, "[YT-DL][Query] Failed to parse json: " + error);
            continue;
        }

        jsons.push_back(move(root));
    }

    /* its a single video */
    if(jsons.empty()) {
        error = json_parse_error.empty() ? "command execution resulted in no result" : json_parse_error;
        return nullptr;
    } else if(jsons.size() == 1) {
        auto root = *jsons.front();

        auto info = make_shared<UrlSongInfo>();
        info->type = UrlType::TYPE_VIDEO;
        info->description = root["description"].asString();
        info->title = root["fulltitle"].asString();
        info->length = std::chrono::seconds{root["duration"].asInt()};
        if(!thumbnail.empty())
            info->thumbnail = std::make_shared<ThumbnailUrl>(thumbnail);
        info->metadata["upload_date"] = root["upload_date"].asString();
        info->metadata["live"] = std::to_string(!root["is_live"].isNull() && root["is_live"].asBool());

        if(root["thumbnail"].isArray() && !root["thumbnail"].empty())
            info->metadata["thumbnail"] = root["thumbnail"][0]["url"].asString();

        return info;
    } else {
        if((*jsons[0])["requested_formats"].isArray()) {
            error = "playlist isn't a playlist format";
            return nullptr;
        }

        auto info = make_shared<UrlPlaylistInfo>();
        info->type = UrlType::TYPE_PLAYLIST;

        size_t entry_id = 0;
        for(const auto& json_ptr : jsons) {
            Json::Value& json = *json_ptr;
            auto entry = make_shared<UrlSongInfo>();
            entry->url = "https://www.youtube.com/watch?v=" + json["id"].asString();
            entry->title = json["title"].asString();
            entry->description = "Playlist entry #" + to_string(++entry_id);
            info->entries.push_back(move(entry));
        }

        return info;
    }
}

threads::Future<std::shared_ptr<music::UrlInfo>> YTVManager::resolve_url_info(const std::string& video) {
    threads::Future<std::shared_ptr<UrlInfo>> future;

    auto config = this->configuration();
    auto command = strvar::transform(config->commands.query_url,
                                     strvar::StringValue{"command", config->youtubedl_command},
                                     strvar::StringValue{"video_url", video}
    );
    cw::execute(command, [future](const cw::Result& result) {
        std::string error{};
        auto info = parse_url_info(result, error);
        if(!info)
            future.executionFailed(error.empty() ? "empty info" : error);
        else
            future.executionSucceed(info);
    }, [future](const std::string& error) {
        future.executionFailed(error);
    });

    return future;
}

threads::Future<std::shared_ptr<music::MusicPlayer>> YTVManager::create_stream(const std::string &video) {
    threads::Future<std::shared_ptr<music::MusicPlayer>> future;

    auto fut = resolve_stream_info(video);
    fut.waitAndGetLater([future, fut](const std::shared_ptr<AudioInfo>& audio){
        if(fut.succeeded() && audio) return future.executionSucceed(make_shared<music::player::YoutubeMusicPlayer>(audio));
        else return future.executionFailed(fut.errorMegssage());
    }, nullptr);

    return future;
}

std::shared_ptr<AudioInfo> parse_stream_info(const cw::Result& result, std::string& error) {
    auto stderr_lines = remove_debug_messages(result.full_stderr);
    auto stdout_lines = remove_debug_messages(result.full_stdout);

    /* Analyzing the response */
    {
        deque<string> warnings;
        for(const auto& line : stderr_lines) {
            if(line.find("ERROR") == std::string::npos)
                continue;

            error = line;
            return nullptr;
        }
    }

    if(stdout_lines.size() < 2) {
        error = "command response is too small";
        return nullptr;
    }

    std::string thumbnail{stdout_lines[stdout_lines.size() - 2]};
    std::string json_data_string{std::string{stdout_lines[stdout_lines.size() - 1]}};

    log::log(log::trace, "[YT-DL] Got thumbnail response: " + thumbnail);
    log::log(log::trace, "[YT-DL] Got json response: " + json_data_string);

    Json::Value root;
    Json::CharReaderBuilder rbuilder;
    std::string json_parse_error;

    istringstream jsonStream(std::string{stdout_lines[stdout_lines.size() - 1]});
    bool parsingSuccessful = Json::parseFromStream(rbuilder, jsonStream, &root, &json_parse_error);
    if (!parsingSuccessful) {
        error = "Failed to parse yt json response. (" + json_parse_error + ")";
        return nullptr;
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
        error = "Failed to get a valid audio stream";
        return nullptr;
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
    return std::make_shared<AudioInfo>(AudioInfo{root["fulltitle"].asString(), "unknown", thumbnail, streamUrl, stream});
}

threads::Future<std::shared_ptr<AudioInfo>> YTVManager::resolve_stream_info(const std::string& video) {
	threads::Future<std::shared_ptr<AudioInfo>> future;

    auto config = this->configuration();
    auto command = strvar::transform(config->commands.query_video,
                                     strvar::StringValue{"command", config->youtubedl_command},
                                     strvar::StringValue{"video_url", video}
    );
    cw::execute(command, [future](const cw::Result& result) {
        std::string error{};
        auto info = parse_stream_info(result, error);
        if(!info)
            future.executionFailed(error.empty() ? "empty info" : error);
        else
            future.executionSucceed(info);
    }, [future](const std::string& error) {
        future.executionFailed(error);
    });

	return future;
}

std::shared_ptr<YTProviderConfig> YTVManager::configuration() const {
    auto config = std::make_shared<YTProviderConfig>();

    auto config_path = fs::u8path("providers/config_youtube.ini");
    if(fs::exists(config_path)) {
        INIReader ini_reader(config_path.string());

        music::log::log(music::log::trace, "[YT-DL] Using config file located at " + config_path.string());
        if(ini_reader.ParseError()) {
            music::log::log(music::log::err, "[YT-DL] Could not parse youtube.ini config! Using default values");
        } else {
            config->youtubedl_command = ini_reader.Get("general", "youtubedl_command", config->youtubedl_command);
            config->commands.version = ini_reader.Get("commands", "version", config->commands.version);
            config->commands.query_video = ini_reader.Get("commands", "query_video", config->commands.query_video);
            config->commands.query_url = ini_reader.Get("commands", "query_url", config->commands.query_url);
        }
    } else {
        music::log::log(music::log::trace, "[YT-DL] Missing configuration file. Using default values");
    }

    return config;
}