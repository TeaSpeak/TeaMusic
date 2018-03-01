#include <map>
#include <misc/pstream.h>
#include "FFMpegMusicPlayer.h"

using namespace std;
using namespace std::chrono;
using namespace music;
using namespace music::player;

FFMpegMusicPlayer::FFMpegMusicPlayer(const std::string& fname) : fname(fname) {
    this->_preferredSampleCount = 960;
}

FFMpegMusicPlayer::FFMpegMusicPlayer(const std::string &a, bool b) : FFMpegMusicPlayer(a) {
    this->live_stream = b;
}

FFMpegMusicPlayer::~FFMpegMusicPlayer() {}

size_t FFMpegMusicPlayer::sampleRate() {
    return 48000;
}

bool FFMpegMusicPlayer::initialize() {
    spawnProcess();
    return this->good();
}

void FFMpegMusicPlayer::play() {
    this->end_reached = false;
    AbstractMusicPlayer::play();
}

void FFMpegMusicPlayer::pause() {
    AbstractMusicPlayer::pause();
}

void FFMpegMusicPlayer::stop() {
    this->destroyProcess();
    AbstractMusicPlayer::stop();
}

PlayerUnits FFMpegMusicPlayer::length() { return this->stream ? this->stream->duration : PlayerUnits(0); }
PlayerUnits FFMpegMusicPlayer::currentIndex() { return this->stream ? milliseconds((int64_t) (this->stream->sampleOffset * 1000.f / this->sampleRate())) : PlayerUnits(0); }
std::string FFMpegMusicPlayer::songTitle() { return this->stream ? this->stream->metadata["title"] : ""; }
std::string FFMpegMusicPlayer::songDescription() { return this->stream ? this->stream->metadata["artist"] + "(" + this->stream->metadata["album"] + ")" : ""; }

void FFMpegMusicPlayer::rewind(const PlayerUnits &duration) {
    threads::MutexLock lock(this->streamLock);
    if(this->currentIndex() < duration) {
        this->seekOffset = PlayerUnits(0);
    } else {
        this->seekOffset = this->currentIndex() - duration;
    }

    if(this->stream) this->spawnProcess();
}
void FFMpegMusicPlayer::forward(const PlayerUnits &duration) {
    threads::MutexLock lock(this->streamLock);
    this->seekOffset = this->currentIndex() + duration;
    if(this->seekOffset > this->length()) {
        this->stop();
        return;
    }

    if(this->stream) this->spawnProcess();
}

bool FFMpegMusicPlayer::finished() { return this->stream == nullptr; }


std::shared_ptr<SampleSegment> FFMpegMusicPlayer::peekNextSegment() {
    threads::MutexLock lock(this->streamLock);
    return this->nextSegment;
}

std::shared_ptr<SampleSegment> FFMpegMusicPlayer::popNextSegment() {
    threads::MutexLock lock(this->streamLock);

    if(this->state() == PlayerState::STATE_STOPPED || !this->stream) {
        auto elm = this->nextSegment;
        this->nextSegment = nullptr;
        return elm;
    }
    if(!this->nextSegment) readNextSegment(milliseconds(1));
    auto elm = this->nextSegment;
    readNextSegment(milliseconds(1));
    if(this->end_reached && !elm && !this->nextSegment) {
        log::log(log::trace, "[FFMPEG] Fire end!");
        this->fireEvent(MusicEvent::EVENT_END);
        this->destroyProcess();
    }
    return elm;
}

extern void trimString(std::string&);
void FFMpegMusicPlayer::readNextSegment(const std::chrono::nanoseconds& max_time) {
    threads::MutexLock lock(this->streamLock);
    auto streamHandle = this->stream;
    if(!streamHandle || this->end_reached) {
        this->nextSegment = nullptr;
        return;
    }

    if(streamHandle->stream->err()) {
        string info;
        auto read = this->readInfo(info, system_clock::now(), "kbits/s ");
        if(read > 0){
            trimString(info);
            if(info[0] == '\r') info = info.substr(1);
            log::log(log::trace, "Read info: " + info);
        } else if((read = this->readInfo(info, system_clock::now(), "Header missing")) > 0){ //possible streaming
            log::log(log::trace, "Dropped error: " + info + "|" + this->errBuff);
            this->errBuff = "";
            auto stream = this->stream;
            char buffer[30];
            while(stream->stream->out().rdbuf()->in_avail() > 0){
                stream->stream->out().readsome(buffer, 30);
            }
            return;
        }

        if(this->errBuff.size() > 64) { //"size=   ?XkB time=XX:XX:XX.XX bitrate=?X.X" -> 42 + n (22) bytes for digits lengths
            this->applayError(this->errBuff);
            this->errBuff.empty();
            return; //May stop process?
        }
        //log::log(log::debug, "Err: " + this->errBuff);
        //TODO close stuff
        //return;
    }

    auto sampleCount = this->preferredSampleCount();
    auto channelCount = streamHandle->channels;

    size_t readLength = sampleCount * channelCount * sizeof(uint16_t); //We have 2 channels
    size_t index = 0;
    auto buffer = static_cast<char *>(malloc(readLength));

    auto beg = system_clock::now();
    while(streamHandle->stream->out().rdbuf()->is_open()){
        if(streamHandle->stream->out().rdbuf()->in_avail() > 0) {
            auto read = streamHandle->stream->out().readsome(&buffer[index], readLength - index);
            if(read > 0) {
                index += read;
                //log::log(log::debug, "Read " + to_string(readLength) + " - " + to_string(index));
                if(index >= readLength) break;
                continue;
            }
        }

        if(streamHandle->stream->out().bad()) {
            this->stop(); //Empty!
            this->fireEvent(MusicEvent::EVENT_END);
            log::log(log::debug, "[FFMPEG] readNextSegment() failed.");
            return;
        }

        if(beg + max_time < system_clock::now()) {
            log::log(log::trace, "[FFMPEG] readNextSegment() -> failed (read loop need more time than allowed)");
            break;
        }
        usleep(250);
    }

    if(index != readLength && index != 0) {
        log::log(log::debug, "[FFMPEG][WARN] readNextSegment() -> No full read! (" + to_string(index) + "/" + to_string(readLength) + ")");
        sampleCount = index / streamHandle->channels / sizeof(uint16_t);
    }

    if(sampleCount > 0 && index > 0){
        streamHandle->sampleOffset += sampleCount;
        auto elm = std::make_shared<SampleSegment>();
        elm->channels = streamHandle->channels;
        elm->segmentLength = sampleCount;
        elm->segments = reinterpret_cast<int16_t *>(buffer);
        this->nextSegment = elm;
    }

    if(!streamHandle->stream->out().rdbuf()->is_open() || !streamHandle->stream->err().rdbuf()->is_open() || index == 0) {
        if(read_success.time_since_epoch().count() == 0) read_success = system_clock::now();

        if(read_success + seconds(1) < system_clock::now()) {
            this->end_reached = true;
            log::log(log::debug, string() + "[FFMPEG] readNextSegment() failed (" + (index == 0 ? "read zero" : "ffmpeg stream closed") + ").");
        }
        return;
    }
    read_success = system_clock::now();
}