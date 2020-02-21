#include <map>
#include <regex>
#include <utility>
#include <StringVariable.h>
#include <teaspeak/MusicPlayer.h>
#include <providers/shared/pstream.h>
#include "./FFMpegMusicPlayer.h"

using namespace std;
using namespace std::chrono;
using namespace music;
using namespace music::player;

FFMpegMusicPlayer::FFMpegMusicPlayer(std::string  fname) : file{std::move(fname)} {
    this->_preferredSampleCount = 960;
}

FFMpegMusicPlayer::FFMpegMusicPlayer(const std::string &a, bool b) : FFMpegMusicPlayer(a) {
    this->live_stream = b;
}

FFMpegMusicPlayer::~FFMpegMusicPlayer() {
    this->destroy_stream();
};

size_t FFMpegMusicPlayer::sampleRate() {
    return 48000;
}

bool FFMpegMusicPlayer::initialize(size_t channel) {
	AbstractMusicPlayer::initialize(channel);
	this->stream_successfull_started = false;
	this->spawn_stream();
    return this->good();
}

void FFMpegMusicPlayer::play() {
    if(!this->stream)
        this->spawn_stream();

    AbstractMusicPlayer::play();
}

void FFMpegMusicPlayer::pause() {
    auto stream_ref = this->stream;
    if(stream_ref) this->start_offset = stream_ref->current_playback_index();
    this->destroy_stream(); //TODO: Add some kind of pause?

    AbstractMusicPlayer::pause();
}

void FFMpegMusicPlayer::stop() {
    this->destroy_stream();
    AbstractMusicPlayer::stop();
}

PlayerUnits FFMpegMusicPlayer::length() {
    return this->cached_stream_info.length;
}

PlayerUnits FFMpegMusicPlayer::currentIndex() {
    auto stream_ref = this->stream;
    if(!stream_ref) return this->start_offset;

    return stream_ref->current_playback_index();
}

PlayerUnits FFMpegMusicPlayer::bufferedUntil() {
    auto stream_ref = this->stream;
    if(!stream_ref) return this->start_offset;

    return stream_ref->current_buffer_index();
}

std::string FFMpegMusicPlayer::songTitle() {
    return this->cached_stream_info.title;
}
std::string FFMpegMusicPlayer::songDescription() {
    return this->cached_stream_info.description;
}

void FFMpegMusicPlayer::rewind(const PlayerUnits &duration) {
    auto stream_ref = this->stream;
    if (!stream_ref) return;

    auto target = stream_ref->current_playback_index() - duration;
    if(target.count() < 0)
        target = PlayerUnits{0};

    this->destroy_stream();

    this->start_offset = target;
    this->spawn_stream();
}
void FFMpegMusicPlayer::forward(const PlayerUnits &duration) {
    auto stream_ref = this->stream;
    if(!stream_ref) return;

    auto target = stream_ref->current_playback_index() + duration;
    auto& info = stream_ref->stream_info();
    if(info.initialized && target > std::chrono::ceil<PlayerUnits>(info.stream_length)) {
        this->stop();
        return;
    }

    this->destroy_stream();

    this->start_offset = target;
    this->spawn_stream();
}

bool FFMpegMusicPlayer::finished() { return this->stream == nullptr; }


std::shared_ptr<SampleSegment> FFMpegMusicPlayer::peekNextSegment() {
    auto stream_ref = this->stream;
    if(!stream_ref) return nullptr;

    return stream_ref->peek_next_segment();
}

std::shared_ptr<SampleSegment> FFMpegMusicPlayer::popNextSegment() {
    auto stream_ref = this->stream;
    if(!stream_ref) goto flush_events;

    if(this->state() == PlayerState::STATE_STOPPED || this->state() == PlayerState::STATE_UNINIZALISIZED)
        goto flush_events;

    if(auto buffer = stream_ref->pop_next_segment(); buffer)
        return buffer;

    flush_events:
    if(this->stream_aborted) {
        this->fireEvent(MusicEvent::EVENT_ABORT);
    } else if(this->stream_ended) {
        this->fireEvent(MusicEvent::EVENT_END);
    }
    this->stream_ended = false;
    this->stream_aborted = false;
    return nullptr;
}

deque<shared_ptr<Thumbnail>> FFMpegMusicPlayer::thumbnails() {
	//TODO generate thumbnails
	return {};
}

void FFMpegMusicPlayer::spawn_stream() {
    std::string error{};

    auto stream = std::make_shared<FFMpegStream>(this->file, this->cached_stream_info.length.count() > 0 ? this->start_offset : PlayerUnits{0}, 960, 2, 48000);
    if(!stream->initialize(error)) {
        this->apply_error(error);
        return;
    }

    stream->callback_info_initialized = std::bind(&FFMpegMusicPlayer::callback_stream_info, this);
    stream->callback_ended = std::bind(&FFMpegMusicPlayer::callback_stream_ended, this);
    stream->callback_abort = std::bind(&FFMpegMusicPlayer::callback_stream_aborted, this);

    this->stream_aborted = false;
    this->stream_ended = false;
    this->cached_stream_info.up2date = false;
    std::swap(this->stream, stream);

    if(stream) {
        stream->callback_info_initialized = nullptr;
        stream->callback_ended = nullptr;
        stream->callback_abort = nullptr;
    }
}

void FFMpegMusicPlayer::destroy_stream() {
    std::exchange(this->stream, nullptr);
}

void FFMpegMusicPlayer::callback_stream_info() {
    if(this->cached_stream_info.up2date) return;

    auto stream_ref = this->stream;
    if(!stream_ref) return;

    auto& info = stream_ref->stream_info();
    std::lock_guard lock{info.lock};
    if(!info.initialized) return;

    this->cached_stream_info.length = info.stream_length;
    this->cached_stream_info.title = "unknown";
    for(const auto& key : {"title", "StreamTitle"}) {
        if(info.metadata.count(key)) {
            this->cached_stream_info.title = info.metadata.at(key);
            break;
        }
    }

    this->cached_stream_info.description = "unknown";
    for(const auto& key : {"artist", "album", "icy-name"}) {
        if(info.metadata.count(key)) {
            this->cached_stream_info.description = info.metadata.at(key);
            break;
        }
    }

    this->stream_successfull_started = true;
    this->stream_fail_count = 0;
    this->fireEvent(EVENT_INFO_UPDATE);
}

void FFMpegMusicPlayer::callback_stream_ended() {
    this->stream_ended = true;
}

void FFMpegMusicPlayer::callback_stream_aborted() {
    this->stream_aborted = true;

    auto stream_ref = this->stream;
    if(!stream_ref) return;

    if(this->stream_successfull_started && this->stream_fail_count++ < 3) {
        log::log(log::debug, "FFmpeg stream aborted. Abort count: " + std::to_string(this->stream_fail_count) + ". Restarting stream.");
        this->start_offset = stream->current_playback_index(); //TODO Save already buffered samples!
        this->spawn_stream();
    } else {
        log::log(log::debug, "FFmpeg stream aborted. Abort count: " + std::to_string(this->stream_fail_count) + ". Stream failed totally.");
    }
}

void FFMpegMusicPlayer::callback_stream_connect_error(const std::string &error) {
    auto stream_ref = this->stream;
    if(!stream_ref) return;

    log::log(log::debug, "FFmpeg failed to connect: " + error);
    if(!this->stream_successfull_started) {
        stream_ref->callback_abort = nullptr; /* we already handle that */
        this->apply_error(error);
        this->fireEvent(MusicEvent::EVENT_ERROR);
        return;
    }
}