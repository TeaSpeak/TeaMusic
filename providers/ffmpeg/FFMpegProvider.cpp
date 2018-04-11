#include "FFMpegProvider.h"
#include "FFMpegMusicPlayer.h"
#include <misc/pstream.h>

using namespace std;
using namespace std::chrono;
using namespace music;

std::string ffmpeg_command = "ffmpeg";
/**
Build your own ffmpeg version:
 apt-get install libfrei0r-ocaml-dev
 apt-get install libgnutls-dev
 apt-get install libiec61883-dev
 apt-get install libass-dev
 apt-get install libbluray-dev
 apt-get install libbs2b-dev
 apt-get install libcaca-dev
 apt-get install libdc1394-22-dev
 apt-get install flite-dev
 apt-get install libgme-dev
 apt-get install libgsm1-dev
 apt-get install libmodplug-dev
 apt-get install libmp3lame-dev
 apt-get install libopencv-dev
 apt-get install libopenjpeg-dev
 apt-get install libopus-dev (Requires 1.2.1)
 apt-get install libpulse-dev
 apt-get install librtmp-dev
 apt-get install libshine-dev
 apt-get install libsoxr-dev
 apt-get install libssh-dev
 apt-get install libspeex-dev
 apt-get install libtheora-dev
 apt-get install libtwolame-dev
 apt-get install libvorbis-dev
 apt-get install libvpx-dev
 apt-get install libwavpack-dev
 apt-get install libwebp-dev
 apt-get install libx264-dev
 apt-get install libx265-dev
 apt-get install libxvidcore-dev
 apt-get install libzmq3-dev
 apt-get install libzvbi-dev
 apt-get install libopenal-dev
 apt-get install libcdio-dev (No usable libcdio/cdparanoia found)
 apt-get install libcdio-paranoia-dev

../configure --cc=cc --cxx=g++ --extra-cflags="-I/usr/include/opus" --extra-ldflags=-lopus --enable-gpl --enable-nonfree --disable-stripping --enable-avresample --enable-avisynth --enable-gnutls --enable-ladspa --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libmodplug --enable-libmp3lame --enable-libopenjpeg --enable-libopus --enable-libpulse --enable-librtmp --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxvid --enable-libzvbi --enable-openal --enable-opengl --enable-libdc1394 --enable-libzmq --enable-frei0r --enable-libx264 --enable-libopencv --enable-openssl --enable-gnutls
 */
inline pair<string, string> executeCommand(const string& cmd){
    redi::pstream proc;
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

threads::Future<std::shared_ptr<music::MusicPlayer>> FFMpegProvider::createPlayer(const std::string &string) {
    auto player = std::make_shared<music::player::FFMpegMusicPlayer>(string);
    auto future = threads::Future<std::shared_ptr<music::MusicPlayer>>();
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

extern void trimString(std::string &str);
inline vector<string> available_protocols(std::string &error) {
    error = "";
    auto vres = executeCommand(ffmpeg_command + " -protocols");

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
        trimString(line);
        if(line == "Output:") break;
        resVec.push_back(line);
    }

    return resVec;
}

inline vector<string> available_fmt(std::string &error) {
    error = "";
    auto vres = executeCommand(ffmpeg_command + " -formats");

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
    string error;
    auto vres = executeCommand(ffmpeg_command + " -version");

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


    auto provider = std::make_shared<FFMpegProvider>();

    auto prots = available_protocols(error);
    if(!error.empty()) {
        log::log(log::err, "[FFMPEG] Could not parse available protocols");
        log::log(log::err, "[FFMPEG] " + error);
    }
    provider->av_protocol = prots;

    auto fmts = available_fmt(error);
    if(!error.empty()) {
        log::log(log::err, "[FFMPEG] Could not parse available formats");
        log::log(log::err, "[FFMPEG] " + error);
    }
    provider->av_fmt = fmts;

    return provider;
}

FFMpegProvider* FFMpegProvider::instance = nullptr;
FFMpegProvider::FFMpegProvider() {
	FFMpegProvider::instance = this;
	this->providerName = "FFMpeg";
	this->providerDescription = "FFMpeg playback support";

	this->readerBase = event_base_new();
	this->readerDispatch = new threads::Thread(THREAD_EXECUTE_LATER | THREAD_SAVE_OPERATIONS, [&](){
		while(this->readerBase) {
			event_base_dispatch(this->readerBase);
			threads::self::sleep_for(milliseconds(10)); //Dont have somethink to do
		}
	});
	this->readerDispatch->name("FFMpeg IO").execute();
}

FFMpegProvider::~FFMpegProvider() {
	FFMpegProvider::instance = nullptr;

    auto base = this->readerBase;
    this->readerBase = nullptr;
    if(base) {
        event_base_loopbreak(base);
        event_base_loopexit(base, nullptr);

        if(this->readerDispatch) {
            if(!this->readerDispatch->join(system_clock::now() + seconds(3))) this->readerDispatch->detach();
            delete this->readerDispatch;
            this->readerDispatch = nullptr;
        }

        event_base_free(base);
    }
}