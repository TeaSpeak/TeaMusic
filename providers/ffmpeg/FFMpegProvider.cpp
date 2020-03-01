#include <experimental/filesystem>
#include <utility>
#include <StringVariable.h>
#include <providers/shared/INIParser.h>
#include <providers/shared/pstream.h>
#include "./string_utils.h"
#include "./FFMpegProvider.h"
#include "./FFMpegMusicPlayer.h"

using namespace std;
using namespace std::chrono;
using namespace music;
namespace fs = std::experimental::filesystem;

inline pair<string, string> executeCommand(const string& cmd){
    redi::pstream proc;
	log::log(log::debug, "[FFMPEG] Executing command \"" + cmd + "\"");
    proc.open(cmd, redi::pstreams::pstdout | redi::pstreams::pstderr);
    string in;
    string err;
    size_t bufferLength = 512;
    char buffer[bufferLength];
    string part;

    auto last_read = system_clock::now(); //Sometimes data is still available after proc end
    do {
        usleep(10);
        if(proc.out().rdbuf()->in_avail() > 0){
            auto read = proc.out().readsome(buffer, bufferLength);
            if(read > 0) {
                in += string(buffer, read);
                last_read = system_clock::now();
            }
        }

        if(proc.err().rdbuf()->in_avail() > 0){
            auto read = proc.err().readsome(buffer, bufferLength);
            if(read > 0) {
                err += string(buffer, read);
                last_read = system_clock::now();
            }
        }
    } while(!proc.rdbuf()->exited() || last_read + milliseconds(50) > system_clock::now());
    return {in, err};
};

threads::Future<std::shared_ptr<music::MusicPlayer>> FFMpegProvider::createPlayer(const std::string &url, void* custom_data, void*) {
	auto future = threads::Future<std::shared_ptr<music::MusicPlayer>>();

	//custom_data
	std::shared_ptr<music::MusicPlayer> player;
	if(!custom_data) {
		player = std::make_shared<music::player::FFMpegMusicPlayer>(url, player::FFMPEGURLType::STREAM, music::player::FFMpegMusicPlayer::FallbackStreamInfo{});
	} else {
		std::shared_ptr<FFMpegData::Header> data;
		{
			void(*free_ptr)(void*) = nullptr;
			auto header = (FFMpegData::Header*) custom_data;

			/* file the free function */
			free_ptr = header->_free;
			if(!free_ptr)
				free_ptr = ::free;
			header->_free = free_ptr;

			data = shared_ptr<FFMpegData::Header>(header, free_ptr);
		}
		if(!data || data->version != FFMpegData::CURRENT_VERSION) {
			future.executionFailed("invalid data or version");
			return future;
		}
		if(data->type == FFMpegData::REPLAY_FILE) {
			auto cast_data = static_pointer_cast<FFMpegData::FileReplay>(data);
            music::player::FFMpegMusicPlayer::FallbackStreamInfo fallback_info{};
            fallback_info.title = cast_data->file_title ? std::string{cast_data->file_title} : "";
            fallback_info.description = cast_data->file_description ? std::string{cast_data->file_description} : "";
			player = std::make_shared<music::player::FFMpegMusicPlayer>(std::string{cast_data->file_path}, player::FFMPEGURLType::FILE, fallback_info);

			/* free content */
            cast_data->_free(cast_data->file_title);
			cast_data->_free(cast_data->file_description);
			cast_data->_free(cast_data->file_path);
		} else {
			future.executionFailed("invalid data type");
			return future;
		}
	}
	if(!player) {
		future.executionFailed("could not create a valid player");
		return future;
	}
    future.executionSucceed(std::dynamic_pointer_cast<music::MusicPlayer>(player));
    return future;
}

inline string part(std::string& str, const std::string& deleimiter, bool invert_search = false) {
    auto index = invert_search ? str.find_first_not_of(deleimiter) : str.find(deleimiter);
    if(index == std::string::npos) return "";

    auto res = str.substr(0, index);
    if(str.length() > index + deleimiter.length())
        str = str.substr(index + (invert_search ? 0 : deleimiter.length()));
    else
        str = "";

    return res;
}

inline vector<string> available_protocols(const std::shared_ptr<music::FFMpegProviderConfig>& config, std::string &error) {
    error = "";
    auto vres = executeCommand(strvar::transform(config->commands.protocols, strvar::StringValue{"command", config->ffmpeg_command}));

    /* Header is print in err stream
    if(!vres.second.empty()) {
        error = vres.second;
        return {};
    }
     */
    auto result = vres.first;

    string junk = part(result, "Input:\n");
    if(!error.empty()) return {};

    string line;
    vector<string> resVec;
    while(!(line = part(result, "\n")).empty()){
        line = strings::trim(line);
        if(line == "Output:") break;
        resVec.push_back(line);
    }

    return resVec;
}

inline vector<string> available_fmt(const std::shared_ptr<music::FFMpegProviderConfig>& config, std::string &error) {
    error = "";
    auto vres = executeCommand(strvar::transform(config->commands.formats, strvar::StringValue{"command", config->ffmpeg_command}));

    /* Header is print in err stream
    if(!vres.second.empty()) {
        error = vres.second;
        return {};
    }
     */
    auto result = vres.first;

    string junk = part(result, "--\n");
    if(!error.empty()) return {};

    string line;
    vector<string> resVec;

    //line fmt: " DE <name>,<...>             <desc>"
    while(!(line = part(result, "\n")).empty()){
        line = line.substr(1); //Trim the first space

        auto type = line.substr(0, 2);
        line = line.substr(3); //Cut out the space
        auto names = part(line, " ");
        auto spaceJunk = part(line, " ", true);
        auto description = line;

        if(type.find('D') == std::string::npos) continue; //FMT not for decoding
        size_t index = 0;
        do {
            auto oldIndex = index;
            index = names.find(',', index);
            resVec.push_back(names.substr(oldIndex, index == std::string::npos ? index : index - oldIndex));
            index++;
        } while(index != 0);
    }

    return resVec;
}

std::shared_ptr<music::manager::PlayerProvider> create_provider() {
	std::shared_ptr<music::FFMpegProviderConfig> config = make_shared<music::FFMpegProviderConfig>();

	{
		auto config_path = fs::u8path("providers/config_ffmpeg.ini");
		music::log::log(music::log::debug, "[FFMPEG] Using config file located at " + config_path.string());
		if(fs::exists(config_path)) {
			INIReader ini_reader(config_path.string());

			if(ini_reader.ParseError()) {
				music::log::log(music::log::err, "[FFMPEG] Could not parse config! Using default values");
			} else {
				config->ffmpeg_command = ini_reader.Get("general", "ffmpeg_command", config->ffmpeg_command);
				config->commands.version = ini_reader.Get("commands", "version", config->commands.version);
				config->commands.protocols = ini_reader.Get("commands", "protocols", config->commands.protocols);
				config->commands.formats = ini_reader.Get("commands", "formats", config->commands.formats);

				config->commands.playback = ini_reader.Get("commands", "playback", config->commands.playback);
				config->commands.playback_seek = ini_reader.Get("commands", "playback_seek", config->commands.playback_seek);

                config->commands.file_playback = ini_reader.Get("commands", "file_playback", config->commands.file_playback);
                config->commands.file_playback_seek = ini_reader.Get("commands", "file_playback_seek", config->commands.file_playback_seek);
				music::log::log(music::log::info, "[FFMPEG] Config successfully loaded");
			}
		} else {
			music::log::log(music::log::debug, "[FFMPEG] Missing configuration file. Using default values");
		}
	}

    string error;
    auto vres = executeCommand(strvar::transform(config->commands.version, strvar::StringValue{"command", config->ffmpeg_command}));

    error = vres.second;
    auto in = vres.first;
    if(error.find('\n') == error.length() - 1) error = error.substr(0, error.length() - 1);
    if(!error.empty()) {
        music::log::log(music::log::err, "[FFMPEG] Could not find ffmpeg (Error: \"" + error + "\")");
        if(error.find("opus_multistream_surround_encoder_create") != std::string::npos) { //Should not happen cuz a new version if opus is in the lib folder :)
            music::log::log(music::log::err, "[FFMPEG] You have to download libopus v1.2.0 (may you should build it by your own!)");
        } else {
            music::log::log(music::log::err, "[FFMPEG] How to download/install ffmpeg: \"sudo apt-get install ffmpeg\"");
        }
        return nullptr;
    }
    music::log::log(music::log::info, "[FFMPEG] Resolved ffmpeg with version \"" + in.substr(0, in.find('\n')) + "\"");


    auto provider = std::make_shared<FFMpegProvider>(config);
    if(!provider->initialize()) return nullptr;

    auto prots = available_protocols(config, error);
    if(!error.empty()) {
        log::log(log::err, "[FFMPEG] Could not parse available protocols");
        log::log(log::err, "[FFMPEG] " + error);
    }
    provider->av_protocol = prots;

    auto fmts = available_fmt(config, error);
    if(!error.empty()) {
        log::log(log::err, "[FFMPEG] Could not parse available formats");
        log::log(log::err, "[FFMPEG] " + error);
    }
    provider->av_fmt = fmts;

    return provider;
}

FFMpegProvider* FFMpegProvider::instance = nullptr;
FFMpegProvider::FFMpegProvider(shared_ptr<FFMpegProviderConfig>  cfg) : config(std::move(cfg)) {
	FFMpegProvider::instance = this;
	this->providerName = "FFMpeg";
	this->providerDescription = "FFMpeg playback support";
}

FFMpegProvider::~FFMpegProvider() {
	FFMpegProvider::instance = nullptr;

    if(this->readerBase) {
        libevent::functions->event_base_loopexit(this->readerBase, nullptr);

        try {
	        this->readerDispatch.join();
        } catch(std::system_error& ex) {
	        if(ex.code() != errc::invalid_argument) /* exception is not about that the thread isn't joinable anymore */
		        log::log(log::critical, "failed to join dispatch thread");
        }

        libevent::functions->event_base_free(this->readerBase);
	    this->readerBase = nullptr;
    }

    libevent::release_functions();
}

bool FFMpegProvider::initialize() {
    std::string error{};
    if(!libevent::resolve_functions(error)) {
        log::log(log::err, "failed to resolve libevent functions: " + error);
        return false;
    }

    this->readerBase = libevent::functions->event_base_new();
    this->readerDispatch = std::thread([&]{
        while(!libevent::functions->event_base_got_exit(this->readerBase))
            libevent::functions->event_base_loop(this->readerBase, 0x04); //EVLOOP_NO_EXIT_ON_EMPTY
    });

#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
    pthread_t handle = this->readerDispatch.native_handle();
    pthread_setname_np(handle, "FFMPeg IO Loop");
#endif
    return true;
}

threads::Future<shared_ptr<UrlInfo>> FFMpegProvider::query_info(const std::string &url, void *custom_data, void *pVoid1) {
    auto future = threads::Future<shared_ptr<UrlInfo>>();

    auto player_fut = createPlayer(url, custom_data, pVoid1);
    player_fut.wait();
    if(player_fut.failed()) {
        future.executionFailed(player_fut.errorMegssage());
    } else {
        auto player = dynamic_pointer_cast<music::player::FFMpegMusicPlayer>(*player_fut.get());
        std::thread([player, future]{
            if(!player->initialize(0)) {
                future.executionFailed("failed to initialize player");
                return;
            }

            auto timeout = std::chrono::system_clock::now();
            timeout += std::chrono::seconds{30};
            if(!player->await_info(timeout)) {
                future.executionFailed("info load timeout");
                return;
            }

            auto info = make_shared<UrlSongInfo>();

            info->type = UrlType::TYPE_VIDEO;
            info->url = player->url();
            info->title = player->songTitle();
            info->description = player->songDescription();
            info->metadata = {};

            future.executionSucceed(info);
        }).detach();
    }

    return future;

}
