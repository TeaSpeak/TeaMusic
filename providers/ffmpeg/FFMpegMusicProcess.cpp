#include <map>
#include <sstream>
#include <misc/pstream.h>
#include "FFMpegMusicPlayer.h"
#include "FFMpegProvider.h"

using namespace std;
using namespace std::chrono;
using namespace music;
using namespace music::player;

extern std::string ffmpeg_command;
const static char* ffmpeg_command_args = "-hide_banner -stats -i \"%1$s\" -vn -bufsize 512k -ac %2$d -ar 48000 -f s16le -acodec pcm_s16le pipe:1"; //-vn = disable video | -bufsize 512k buffer audio
const static char* ffmpeg_command_args_seek = "-hide_banner -ss %1$s -stats -i \"%2$s\" -vn -bufsize 512k -ac %3$d -ar 48000 -f s16le -acodec pcm_s16le pipe:1"; //-vn = disable video | -bufsize 512k buffer audio
//TODO Channel count variable

std::string replaceString(std::string subject, const std::string& search, const std::string& replace) {
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::string::npos) {
        subject.replace(pos, search.length(), replace);
        pos += replace.length();
    }
    return subject;
}

void trimString(std::string &str) {
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
        log::log(log::debug, "[FFMPEG][" + to_string(this) + "] Full error log: \n" + this->errHistory); \
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
    sprintf(buffer, "%02d:%02d:%02d.%02d", (int) hour.count(), (int) minute.count(), (int) second.count(), (int) milli.count() / 10);
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


     ---------------------
     Could be also like this:

[19:43:01] [TRACE] Input #0, mp3, from 'http://stream01.iloveradio.de/iloveradio1.mp3':
[19:43:01] [TRACE]   Metadata:
[19:43:01] [TRACE]     StreamTitle     : SEAN PAUL - GOT 2 LUV U
[19:43:01] [TRACE]     icy-br          : 128
[19:43:01] [TRACE]     icy-name        : I Love Radio - Charts & Hits by iloveradio.de
[19:43:01] [TRACE]     icy-pub         : -1
[19:43:01] [TRACE]     icy-url         : iloveradio.de
[19:43:01] [TRACE]   Duration: N/A, start: 0.000000, bitrate: 128 kb/s
[19:43:01] [TRACE]     Stream #0:0: Audio: mp3, 44100 Hz, stereo, s16p, 128 kb/s
[19:43:01] [TRACE] Stream mapping:
[19:43:01] [TRACE]   Stream #0:0 -> #0:0 (mp3 (native) -> pcm_s16le (native))
[19:43:01] [TRACE] Press [q] to stop, [?] for help
  */

void FFMpegMusicPlayer::destroyProcess() {
    threads::MutexLock lock(this->streamLock);
    if(!this->stream) return;

    this->end_reached = true;
    if(this->stream->stream) this->stream->stream;
    this->stream = nullptr;
    this->end_reached = false;

    this->errBuff = "";
    this->errHistory = "";
	this->bufferedSamples.clear();
}

#define ARGUMENT_BUFFER_LENGTH 2048
void FFMpegMusicPlayer::spawnProcess() {
    threads::MutexLock lock(this->streamLock);
    this->destroyProcess();
	this->bufferedSamples.clear();
    this->end_reached = false;


	auto argumentBuffer = new char[ARGUMENT_BUFFER_LENGTH];
	auto seek_offset = buildTime(this->seekOffset);
	if(this->seekOffset.count() > 0) {
		snprintf(argumentBuffer, ARGUMENT_BUFFER_LENGTH, ffmpeg_command_args_seek, seek_offset.c_str(), this->fname.c_str(), this->_channelCount);
	} else {
		snprintf(argumentBuffer, ARGUMENT_BUFFER_LENGTH, ffmpeg_command_args, this->fname.c_str(), this->_channelCount);
	}
    auto cmd = ffmpeg_command + " " + string(argumentBuffer);
	delete[] argumentBuffer;

    log::log(log::debug, "[FFMPEG][" + to_string(this) + "] Executing command: " + cmd);
    this->stream = std::make_shared<FFMpegStream>(new redi::pstream(cmd, redi::pstreams::pstdin | redi::pstreams::pstderr | redi::pstreams::pstdout));
    auto self = this->stream;
    self->channels = this->_channelCount;
    log::log(log::debug, "[FFMPEG][" + to_string(this) + "] Awaiting info");

    string info;
    auto read = this->readInfo(info, system_clock::now() + seconds(5), "Metadata:\n");
    PERR("Could not get metadata tag (" + this->errBuff + ")");

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

    log::log(log::trace, "Parsed video/stream info for \"" + this->fname + "\". Full string:\n" + this->errHistory);

	this->stream->eventBase = FFMpegProvider::instance->readerBase; //TODO pool?
	this->stream->initializeEvents();

	this->stream->callback_read_error = std::bind(&FFMpegMusicPlayer::callback_read_err, this, placeholders::_1);
	this->stream->callback_read_output = std::bind(&FFMpegMusicPlayer::callback_read_output, this, placeholders::_1);
	this->stream->callback_end = std::bind(&FFMpegMusicPlayer::callback_end, this);
	this->stream->enableBuffering(); //TODO buffer strategy?
}

ssize_t FFMpegMusicPlayer::readInfo(std::string& result, const std::chrono::system_clock::time_point& timeout, std::string delimiter) {
    threads::MutexLock lock(this->streamLock);
    auto stream = this->stream;
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
        while(/*stream->stream->err().rdbuf()->in_avail() > 0*/ true){
            ssize_t read = stream->stream->err().readsome(buffer, bufferLength);
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
    } while(!stream->stream->rdbuf()->exited());

    if(!delimiter.empty()) { //eof and not found delimiter
        this->errBuff = result;
        result = "";
        return 0;
    }
    return _read;
}
inline bool enableNonBlock(int fd){
	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	return true;
}

FFMpegStream::~FFMpegStream() {
	if(this->stream) this->stream->rdbuf()->kill();
	delete stream;
	this->stream = nullptr;

	if(outEvent) {
		event_del(outEvent);
		event_free(outEvent);
	}
	if(errEvent) {
		event_del(errEvent);
		event_free(errEvent);
	}
}

bool FFMpegStream::initializeEvents() {
	if(!this->eventBase) {
		log::log(log::critical, "Could not initialise FFMpeg Stream without an event base!");
		return false;
	}

	auto fd_err = this->stream->rdbuf()->rpipe(redi::basic_pstreambuf<char>::buf_read_src::rsrc_err);
	auto fd_out = this->stream->rdbuf()->rpipe(redi::basic_pstreambuf<char>::buf_read_src::rsrc_out);
	enableNonBlock(fd_err);
	enableNonBlock(fd_out);
	log::log(log::debug, "Got ffmpeg file descriptors for err " + to_string(fd_err) + " and out " + to_string(fd_out));
	if(fd_err > 0)
		this->errEvent = event_new(this->eventBase, fd_err, EV_READ | EV_PERSIST, FFMpegStream::callbackfn_read_error, this);
	if(fd_out > 0)
		this->outEvent = event_new(this->eventBase, fd_out, EV_READ | EV_PERSIST, FFMpegStream::callbackfn_read_output, this);
	return true;
}

void FFMpegStream::callback_read(int fd, bool error) {
	ssize_t bufferLength = 1024;
	char buffer[1024];
	bufferLength = read(fd, buffer, bufferLength);
	if(bufferLength <= 0) {
		log::log(log::err, "Invalid read (error). Length: " + to_string(bufferLength) + " Code: " + to_string(errno) + " Message: " + strerror(errno));
		if(bufferLength == 0 && errno == 0)
			this->callback_end();
		else
			this->callback_error(error ? IO_ERROR : IO_OUTPUT, bufferLength, error, strerror(errno));
		this->disableBuffering();
		return;
	}

	if(error)
		this->callback_read_error(string(buffer, bufferLength));
	else
		this->callback_read_output(string(buffer, bufferLength));
}

void FFMpegStream::callbackfn_read_output(int fd, short, void* ptrHandle) {
	((FFMpegStream*) ptrHandle)->callback_read(fd, false);
}

void FFMpegStream::callbackfn_read_error(int fd, short, void *ptrHandle) {
	((FFMpegStream*) ptrHandle)->callback_read(fd, true);
}