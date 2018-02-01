#include <map>
#include <sstream>
#include <misc/pstream.h>
#include "FFMpegMusicPlayer.h"

using namespace std;
using namespace std::chrono;
using namespace music;
using namespace music::player;

extern std::string ffmpeg_command;
const static char* ffmpeg_command_args = "-hide_banner -ss %1$s -stats -i \"%2$s\" -vn -bufsize 512k -ac 2 -ar 48000 -f s16le -acodec pcm_s16le pipe:1"; //-vn = disable video | -bufsize 512k buffer audio

std::string replaceString(std::string subject, const std::string& search, const std::string& replace) {
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::string::npos) {
        subject.replace(pos, search.length(), replace);
        pos += replace.length();
    }
    return subject;
}

inline void trimString(std::string &str) {
    size_t endOff = str.length();
    size_t begOff = 0;
    while(endOff > 0 && str[endOff - 1] == ' ') endOff--;
    while(begOff < endOff && str[begOff] == ' ') begOff++;
    str = str.substr(begOff, endOff - begOff);
}

#define PERR(message) \
do {\
    if(read <= 0) { \
        stringstream ss; \
        ss << "Could not spawn new ffmpeg process.\n  Message: " << message; \
        if(!this->errBuff.empty()) { \
            ss << "\n  Additional error:\n    "; \
            ss << replaceString(this->errBuff, "\n", "\n    ") << endl; \
        } \
        this->applayError(ss.str()); \
        log::log(log::debug, "[FFMPEG] Full error log: \n" + this->errHistory); \
        return; \
    } \
} while(0)


inline std::map<string, string> parseMetadata(const std::string& in){
    std::map<string, string> result;

    size_t index = 0;
    do {
        auto oldIndex = index;
        index = in.find('\n', index);
        string line = in.substr(oldIndex, index - oldIndex);
        if(line.find_first_not_of(' ') == std::string::npos) continue; //Empty line

        auto seperator = line.find_first_of(':');
        string key = line.substr(0, seperator);
        string value = line.substr(seperator + 1);
        trimString(key);
        trimString(value);

        result[key] = value;
    } while(++index != 0);

    return result;
}

/**
 * @param time format: '00:03:53.50'
 * @return
 */
inline PlayerUnits parseTime(const std::string& time){
    int hour, minute, second, milli;
    if(sscanf(time.c_str(), "%2d:%2d:%2d.%2d",
              &hour,
              &minute,
              &second,
              &milli) != 4) return PlayerUnits(0);
    return milliseconds(milli * 10) +  seconds(second) + minutes(minute) + hours(hour);
}

inline std::string buildTime(PlayerUnits units){
    auto hour = duration_cast<hours>(units);
    units -= hour;

    auto minute = duration_cast<minutes>(units);
    units -= minute;

    auto second = duration_cast<seconds>(units);
    units -= second;

    auto milli = duration_cast<milliseconds>(units);

    char buffer[11 + 1];
    sprintf(buffer, "%02d:%02d:%02d.%02d", hour, minute, second, milli / 10);
    return string(buffer);
}

/*
   Example valid MP3 file data:
     [mp3 @ 0x671fe0] Skipping 0 bytes of junk at 4722.
     Input #0, mp3, from 'test.mp3':
       Metadata:
         title           : Princess
         artist          : BooztKidz
         album           : Test-Albunm
       Duration: 00:03:54.16, start: 0.025057, bitrate: 192 kb/s
         Stream #0:0: Audio: mp3, 44100 Hz, stereo, s16p, 192 kb/s
         Metadata:
           encoder         : Lavc56.10
     Output #0, s16le, to 'pipe:1':
       Metadata:
         title           : Princess
         artist          : BooztKidz
         album           : Test-Albunm
         encoder         : Lavf56.40.101
         Stream #0:0: Audio: pcm_s16le, 48000 Hz, stereo, s16, 1536 kb/s
         Metadata:
           encoder         : Lavc56.60.100 pcm_s16le
     Stream mapping:
       Stream #0:0 -> #0:0 (mp3 (native) -> pcm_s16le (native))
     Press [q] to stop, [?] for help
  */

void FFMpegMusicPlayer::destroyProcess() {
    if(!this->stream) return;

    this->end_reached = true;
    if(this->stream->stream) this->stream->stream;
    this->stream = nullptr;
    this->end_reached = false;
}

void FFMpegMusicPlayer::spawnProcess() {
    this->destroyProcess();
    this->nextSegment = nullptr;
    this->end_reached = false;

    char commandBuffer[1024];
    sprintf(commandBuffer, ffmpeg_command_args, buildTime(this->seekOffset).c_str(), this->fname.c_str());
    auto cmd = ffmpeg_command + " " + string(commandBuffer);
    log::log(log::debug, "[FFMpeg] Executing command: " + cmd);
    this->stream = std::make_shared<FFMpegStream>(new redi::pstream(cmd, redi::pstreams::pstdin | redi::pstreams::pstderr | redi::pstreams::pstdout));
    auto self = this->stream;
    self->channels = 2; //proc spawned with two channels
    self->sampleOffset = duration_cast<milliseconds>(this->seekOffset).count() * this->sampleRate() / 1000;
    log::log(log::debug, "[FFMpeg] Awaiting info");

    string info;
    auto read = this->readInfo(info, system_clock::now() + seconds(5), "Metadata:\n");
    PERR("Could not get metadata tag");

    //Duration parsing
    if(live_stream) {
        read = this->readInfo(info, system_clock::now() + seconds(5), "Stream #");
        PERR("Could not find stream begin");
    } else {
        read = this->readInfo(info, system_clock::now() + seconds(5), "Duration: ");
        PERR("Could not find metadata");
    }

    self->metadata = parseMetadata(info);
    log::log(log::debug, "Available metadata:");
    for(const auto& entry : self->metadata)
        log::log(log::info, " Key: '" + entry.first + "' Value: '" + entry.second + "'");

    if(!live_stream) {
        read = this->readInfo(info, system_clock::now() + seconds(5), ", ");
        PERR("Could not get duration");
        self->duration = parseTime(info);
        log::log(log::info, "Duration: " + info + " | " + to_string(duration_cast<seconds>(parseTime(info)).count()) + " Seconds");
    }
    read = this->readInfo(info, system_clock::now() + seconds(5), "Press [q] to stop, [?] for help\n");
    PERR("Could not read junk data");


    log::log(log::trace, "Parsed video/lstream info for \"" + this->fname + "\". Full string:\n" + this->errHistory);
}

ssize_t FFMpegMusicPlayer::readInfo(std::string& result, const std::chrono::system_clock::time_point& timeout, std::string delimiter) {
    ssize_t _read = 0;
    result = "";
    if(!this->errBuff.empty()) {
        auto index = this->errBuff.find(delimiter);
        if(index != std::string::npos && !delimiter.empty()) {
            result += this->errBuff.substr(0, index);
            _read += index;
            this->errBuff = index + delimiter.length() < this->errBuff.length() ? this->errBuff.substr(index + delimiter.length()) : "";
            return _read;
        }
        result += this->errBuff;
        _read += this->errBuff.length();
        this->errBuff = "";
    }

    size_t bufferLength = 128;
    char buffer[bufferLength];
    do {
        while(this->stream->stream->err().rdbuf()->in_avail() > 0){
            ssize_t read = this->stream->stream->err().readsome(buffer, bufferLength);
            if(read <= 0) break;

            string readStr = string(buffer, read);
#ifdef DEBUG_FFMPEG
            this->errHistory += readStr;
            //log::log(log::debug, "Read: " + readStr);
#endif
            string prefix = result.substr(result.length() < delimiter.size() ? 0 : result.length() - delimiter.length());
            string merged = prefix + readStr;

            ssize_t index = merged.find(delimiter);
            if(!delimiter.empty() && index != std::string::npos) {
                ssize_t endOff = index - prefix.length();
                //log::log(log::debug, "Found at " + to_string(index) + "/" + to_string(endOff));
                if(endOff > 0) {
                    result += readStr.substr(0, endOff);
                    //log::log(log::debug, "Result 0: '" + result + "' | '" + readStr.substr(0, endOff) + "'");
                    _read += endOff;
                    this->errBuff = endOff + delimiter.length() < readStr.length() ? readStr.substr(endOff + delimiter.length()) : "";
                } else {
                    auto off = endOff + delimiter.length();
                    this->errBuff = off < 0 ? result.substr(result.length() + off) : "" + readStr.substr(off > 0 ? off : 0);
                    result = result.substr(0, result.length() + endOff);
                    _read += endOff;
                }
                //log::log(log::debug, "err buff '" + errBuff + "' len -> " + to_string(_read));
                //log::log(log::debug, "Result: '" + result + "'");
                return _read;
            } else {
                result += readStr;
                _read += readStr.length();
            }
        }
        if(timeout.time_since_epoch().count() == 0) {
            if(delimiter.empty()) return _read;
        } else if(system_clock::now() >= timeout) {
            if(!delimiter.empty()) { //timeouted
                this->errBuff = result; //Could not get stuff
                //log::log(log::debug, "Push back again '" + this->errBuff + "'");
                return 0;
            }
            return _read;
        }
        usleep(1000);
    } while(!this->stream->stream->rdbuf()->exited());

    if(!delimiter.empty()) { //eof and not found delimiter
        this->errBuff = result;
        result = "";
        return 0;
    }
    return _read;
}