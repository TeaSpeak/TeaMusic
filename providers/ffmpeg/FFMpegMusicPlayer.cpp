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

FFMpegMusicPlayer::FFMpegMusicPlayer(std::string fname, FFMPEGURLType type, FallbackStreamInfo fallback) : url_{std::move(fname)}, url_type{type}, fallback_stream_info{std::move(fallback)} {
    this->_preferredSampleCount = 960;
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

bool FFMpegMusicPlayer::await_info(const std::chrono::system_clock::time_point &timeout) const {
    std::unique_lock ilock{this->cached_stream_info.cv_lock};
    if(this->cached_stream_info.up2date) return true;

    while(true) {
        if(this->cached_stream_info.update_cv.wait_until(ilock, timeout) == std::cv_status::timeout)
            return false;

        if(this->cached_stream_info.up2date)
            return true;
    }
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
    return this->cached_stream_info.has_title ? this->cached_stream_info.title : this->fallback_stream_info.title;
}
std::string FFMpegMusicPlayer::songDescription() {
    return this->cached_stream_info.has_description ? this->cached_stream_info.description : this->fallback_stream_info.description;
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

    auto stream = std::make_shared<FFMpegStream>(this->url_, this->url_type, this->cached_stream_info.length.count() > 0 ? this->start_offset : PlayerUnits{0}, 960, 2, 48000);
    if(!stream->initialize(error)) {
        this->apply_error(error);
        return;
    }

    stream->callback_info_initialized = std::bind(&FFMpegMusicPlayer::callback_stream_info, this);
    stream->callback_ended = std::bind(&FFMpegMusicPlayer::callback_stream_ended, this);
    stream->callback_abort = std::bind(&FFMpegMusicPlayer::callback_stream_aborted, this);
    stream->callback_connect_error = std::bind(&FFMpegMusicPlayer::callback_stream_connect_error, this, std::placeholders::_1);

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
    auto old_stream = std::exchange(this->stream, nullptr);
    if(!old_stream) return;

    old_stream->callback_info_initialized = nullptr;
    old_stream->callback_ended = nullptr;
    old_stream->callback_abort = nullptr;
}

void FFMpegMusicPlayer::callback_stream_info() {
    std::lock_guard info_lock{this->cached_stream_info.cv_lock};
    this->cached_stream_info.update_cv.notify_all();
    if(this->cached_stream_info.up2date) return;

    auto stream_ref = this->stream;
    if(!stream_ref) return;

    auto& info = stream_ref->stream_info();
    std::lock_guard lock{info.lock};
    if(!info.initialized) return;

    this->cached_stream_info.length = info.stream_length;

    this->cached_stream_info.has_title = false;
    for(const auto& key : {"title", "StreamTitle"}) {
        if(info.metadata.count(key)) {
            this->cached_stream_info.title = info.metadata.at(key);
            this->cached_stream_info.has_title = true;
            break;
        }
    }

    this->cached_stream_info.has_description = false;
    for(const auto& key : {"artist", "album", "icy-name"}) {
        if(info.metadata.count(key)) {
            this->cached_stream_info.description = info.metadata.at(key);
            this->cached_stream_info.has_description = true;
            break;
        }
    }

    this->cached_stream_info.up2date = true;
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
        this->apply_error("failed to reconnect to stream");
        this->fireEvent(MusicEvent::EVENT_ERROR);
    }
}

void FFMpegMusicPlayer::callback_stream_connect_error(const std::string &error) {
    auto stream_ref = this->stream;
    if(!stream_ref) return;

    /* If we know the stream is valid retry it. The callback_stream_aborted will be called after the connect error */
    if(this->stream_successfull_started)
        return;

    log::log(log::debug, "FFMpeg failed to connect: " + error);
    this->apply_error(error);
    this->fireEvent(MusicEvent::EVENT_ERROR);
}