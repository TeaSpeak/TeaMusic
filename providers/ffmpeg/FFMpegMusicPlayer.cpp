#include <map>
#include <regex>
#include <include/MusicPlayer.h>
#include "providers/shared/pstream.h"
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

FFMpegMusicPlayer::~FFMpegMusicPlayer() {
	this->destroyProcess();
};

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
PlayerUnits FFMpegMusicPlayer::currentIndex() { return this->stream ? this->seekOffset + milliseconds((int64_t) (this->sampleOffset * 1000.f / this->sampleRate())) : PlayerUnits(0); }
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

const static auto property_regex = []() -> std::shared_ptr<std::regex> {
	try {
		return make_shared<std::regex>(R"((size|time|bitrate|speed)=([ \t]+)?([a-zA-Z0-9\:\.\,\/]+)[ \t]+)");
	} catch (std::exception& ex) {
		log::log(log::err, "[FFMPEG] Could not compile property regex!");
	}
	return nullptr;
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
		if(property_regex) {
			auto properties_begin = std::sregex_iterator(line.begin(), line.end(), *property_regex);
			auto properties_end = std::sregex_iterator();
			if(properties_begin != properties_end) {
				log::log(log::trace, "[FFMPEG][" + to_string(this) + "] Got " + to_string(std::distance(properties_begin, properties_end)) + " property values on err stream. (Attention: These properties may differ with the known expected properties!)");
				for(auto index = properties_begin; index != properties_end; index++) {
					if(index->length() < 3) {
						log::log(log::trace, "[FFMPEG][" + to_string(this) + "] - <invalid group size for \"" + index->str() + "\">");
						continue;
					}
					log::log(log::trace, "[FFMPEG][" + to_string(this) + "] - " + index->operator[](1).str() + " => " + index->operator[](3).str());
				}
				continue;
			}
		}
		if(!error_send) {
			log::log(log::err, "[FFMPEG][" + to_string(this) + "] Got error message from FFMpeg:");
			error_send = true;
		}
		log::log(log::err, "[FFMPEG][" + to_string(this) + "] " + constBuffer);
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
		log::log(log::critical, "[FFMPEG][" + to_string(this) + "] Invalid read (overflow!) Application could crash");

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
		log::log(log::debug, "[FFMPEG][" + to_string(this) + "] Stop buffering");
		this->stream->disableBuffering();
	}
	if(bufferedSeconds < 10 && !this->stream->buffering) {
		log::log(log::debug, "[FFMPEG][" + to_string(this) + "] Start buffering");
		this->stream->enableBuffering();
	}
}

deque<shared_ptr<Thumbnail>> FFMpegMusicPlayer::thumbnails() {
	//TODO generate thumbnails
	return {};
}
