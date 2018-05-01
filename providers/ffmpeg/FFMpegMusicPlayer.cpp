#include <map>
#include <misc/pstream.h>
#include <include/MusicPlayer.h>
#include <regex>
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

FFMpegMusicPlayer::~FFMpegMusicPlayer() = default;

size_t FFMpegMusicPlayer::sampleRate() {
    return 48000;
}

bool FFMpegMusicPlayer::initialize(size_t channel) {
	AbstractMusicPlayer::initialize(channel);
    spawnProcess();
    return this->good();
}

void FFMpegMusicPlayer::play() {
    if(!this->stream) this->spawnProcess();
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
PlayerUnits FFMpegMusicPlayer::currentIndex() { return this->stream ? milliseconds((int64_t) (this->sampleOffset * 1000.f / this->sampleRate())) : PlayerUnits(0); }
PlayerUnits FFMpegMusicPlayer::bufferedUntil() {
	if(!this->stream) return PlayerUnits(0);
	return this->currentIndex() + milliseconds((int64_t) (this->bufferedSampleCount() * 1000.f / this->sampleRate())) ;
}

size_t FFMpegMusicPlayer::bufferedSampleCount() {
	threads::MutexLock lock(this->sampleLock);
	size_t result = 0;
	for(const auto& buffer : this->bufferedSamples)
		result += buffer->segmentLength;
	return result;
}

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
    threads::MutexLock lock(this->sampleLock);
	if(this->bufferedSamples.empty()) return nullptr;
    return this->bufferedSamples.front();
}

std::shared_ptr<SampleSegment> FFMpegMusicPlayer::popNextSegment() {
    threads::MutexLock lock(this->sampleLock);

    if(this->state() == PlayerState::STATE_STOPPED || this->state() == PlayerState::STATE_UNINIZALISIZED) return nullptr;
	if(this->bufferedSamples.empty()) {
		if(this->end_reached){
			this->fireEvent(MusicEvent::EVENT_END);
			this->stop();
		}
		return nullptr;
	}
	if(this->bufferedSamples.front()->full) {
		std::shared_ptr<SampleSegment> front = this->bufferedSamples[0];
		this->bufferedSamples.pop_front();
		this->sampleOffset += front->segmentLength;
		this->updateBufferState();
		return front;
	}
	return nullptr;
}

extern void trimString(std::string&);

/*
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
*/

const static std::regex timeline_regex = []() -> std::regex {
	try {
		return std::regex(R"([ ]{0,}size=[ ]+[0-9]+kB[ ]+time=[0-9]+:[0-9]{2}:[0-9]{2}(\.[0-9]+)?[ ]+bitrate=[0-9]+(\.[0-9]+)kbits/s[^\x00]+)");
	} catch (std::exception& ex) {
		log::log(log::err, "Could not compile timeline regex!");
		return std::regex("");
	}
}();

void FFMpegMusicPlayer::callback_read_err(const std::string& constBuffer) {
	deque<string> lines;
	size_t index = 0;
	do {
		auto found = constBuffer.find('\n', index);
		lines.push_back(constBuffer.substr(index, found - index));
		index = found + 1;
	} while(index != 0);

	bool error_send = false;
	for(const auto& line : lines) {
		if(std::regex_match(line, timeline_regex)) continue;
		if(!error_send) {
			log::log(log::err, "Got error message from FFMpeg:");
			error_send = true;
		}
		log::log(log::err, constBuffer);
	}
}

void FFMpegMusicPlayer::callback_read_output(const std::string& constBuffer) {
	std::string buffer = constBuffer;

	//log::log(log::debug, "Got " + to_string(buffer.length()) + " bytes");

	threads::MutexLock lock(this->sampleLock);
	std::shared_ptr<SampleSegment> currentSegment = nullptr;
	if(!this->bufferedSamples.empty() && !this->bufferedSamples.back()->full)
		currentSegment = this->bufferedSamples.back();

	if(this->byteBufferIndex > 0) {
		buffer = std::string(this->byteBuffer, this->byteBufferIndex) + buffer;
		this->byteBufferIndex = 0;
	}

	auto availableSamples = buffer.length() / sizeof(uint16_t) / this->_channelCount;
	size_t readBufferIndex = 0;

	while(availableSamples > 0){
		if(!currentSegment) {
			currentSegment = SampleSegment::allocate(this->_preferredSampleCount, this->_channelCount);
			currentSegment->full = false;
			this->bufferedSamples.push_back(currentSegment);
		}

		auto samplesLeft = currentSegment->maxSegmentLength - currentSegment->segmentLength;
		auto samplesToRead = min(samplesLeft, availableSamples);

		auto targetIndex = currentSegment->segmentLength * currentSegment->channels;
		auto copyLength = this->_channelCount * samplesToRead * sizeof(uint16_t);

		memcpy((void *) &currentSegment->segments[targetIndex], &buffer[readBufferIndex], copyLength);

		readBufferIndex += copyLength;
		availableSamples -= samplesToRead;

		currentSegment->segmentLength += samplesToRead;
		if(currentSegment->segmentLength == currentSegment->maxSegmentLength) {
			currentSegment->full = true;
			currentSegment = nullptr;
		}
	}

	if(readBufferIndex < buffer.length()) {
		auto overhead = buffer.length() - readBufferIndex;
		//log::log(log::debug, "Got overhead " + to_string(overhead));
		memcpy(this->byteBuffer, &buffer[readBufferIndex], overhead);
		this->byteBufferIndex = overhead;
	}
	if(readBufferIndex > buffer.length())
		log::log(log::critical, "Invalid read (overflow!) Application could crash");

	this->updateBufferState();
}


void FFMpegMusicPlayer::callback_end() {
	this->end_reached = true;

	threads::MutexLock lock(this->sampleLock);
	if(this->bufferedSamples.empty()) return;
	this->bufferedSamples.back()->full = true;
}

void FFMpegMusicPlayer::updateBufferState() {
	if(this->end_reached || !this->stream) return;

	auto bufferedSamples = this->bufferedSampleCount();
	auto bufferedSeconds = bufferedSamples / this->sampleRate();
	if(bufferedSeconds > 20 && this->stream->buffering) {
		log::log(log::debug, "Stop buffering");
		this->stream->disableBuffering();
	}
	if(bufferedSeconds < 10 && !this->stream->buffering) {
		log::log(log::debug, "Start buffering");
		this->stream->enableBuffering();
	}
}

deque<shared_ptr<Thumbnail>> FFMpegMusicPlayer::thumbnails() {
	//TODO generate thumbnails
	return {};
}
