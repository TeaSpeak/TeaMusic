#include <map>
#include <providers/shared/pstream.h>
#include "./FFMpegMusicPlayer.h"
#include "./FFMpegProvider.h"

using namespace std;
using namespace std::chrono;
using namespace music;
using namespace music::player;

inline bool enable_non_block(int fd) {
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	return true;
}

FFMpegProcessHandle::~FFMpegProcessHandle() {
	this->finalize();
}

void FFMpegProcessHandle::finalize() {
    const auto is_event_thread = std::this_thread::get_id() == this->io.event_thread;
    unique_lock io_lock{this->io.lock, defer_lock};

    if(!is_event_thread) io_lock.lock();
    auto event_out = std::exchange(this->io.event_out, nullptr);
    auto event_err = std::exchange(this->io.event_err, nullptr);
    auto event_timer = std::exchange(this->io.event_timer, nullptr);
    if(io_lock.owns_lock()) io_lock.unlock();

    const auto delete_function = is_event_thread ? libevent::functions->event_del_noblock : libevent::functions->event_del_block;
    if(event_out) {
        delete_function(event_out);
        libevent::functions->event_free(event_out);
    }
    if(event_err) {
        delete_function(event_err);
        libevent::functions->event_free(event_err);
    }
    if(event_timer) {
        delete_function(event_timer);
        libevent::functions->event_free(event_timer);
    }
}

bool FFMpegProcessHandle::initialize_events() {
	if(!this->io.event_base) {
		log::log(log::critical, "Could not initialise FFMpeg Stream without an event base!");
		return false;
	}

	auto fd_err = this->process_handle->rdbuf()->rpipe(redi::basic_pstreambuf<char>::buf_read_src::rsrc_err);
	auto fd_out = this->process_handle->rdbuf()->rpipe(redi::basic_pstreambuf<char>::buf_read_src::rsrc_out);
    enable_non_block(fd_err);
    enable_non_block(fd_out);

	log::log(log::debug, "Got ffmpeg file descriptors for err " + to_string(fd_err) + " and out " + to_string(fd_out));
	if(fd_err > 0)
		this->io.event_err = libevent::functions->event_new(this->io.event_base, fd_err, EV_READ | EV_PERSIST, [](int fd, short, void* _handle) {
		    auto handle = reinterpret_cast<FFMpegProcessHandle*>(_handle);
		    handle->callback_read(fd, true);
        }, this);
	if(fd_out > 0)
		this->io.event_out = libevent::functions->event_new(this->io.event_base, fd_out, EV_READ | EV_PERSIST, [](int fd, short, void* _handle) {
            auto handle = reinterpret_cast<FFMpegProcessHandle*>(_handle);
            handle->callback_read(fd, false);
        }, this);

	this->io.event_timer = libevent::functions->event_new(this->io.event_base, -1, 0, [](int, short, void* _handle) {
        auto handle = reinterpret_cast<FFMpegProcessHandle*>(_handle);
        if(auto callback{handle->callback_timer}; callback)
            callback();
	}, this);

	if(!this->io.event_out) {
        log::log(log::err, "Missing output file descriptor");
        return false;
	}

    if(this->io.event_err) libevent::functions->event_add(this->io.event_err, nullptr);
	return true;
}

void FFMpegProcessHandle::callback_read(int fd, bool is_err_stream) {
	ssize_t read_buffer_length = 1024;
	char buffer[1024];

    read_buffer_length = read(fd, buffer, read_buffer_length);
	if(read_buffer_length <= 0) {
        if(this->io.event_out) libevent::functions->event_del_noblock(this->io.event_out);
        if(this->io.event_err) libevent::functions->event_del_noblock(this->io.event_err);

        auto exited = this->process_handle->rdbuf()->exited();
        auto code = this->process_handle->rdbuf()->status();
		log::log(log::err, "Invalid read (error). Length: " + to_string(read_buffer_length) + " Code: " + to_string(errno) + " Message: " + strerror(errno) + ": Exit: " + std::to_string(exited) + " (" + std::to_string(code) + ")");

		if(exited && code != 0)
		    this->callback_error(ErrorCode::UNEXPECTED_EXIT, code);
		else if(read_buffer_length == 0)
			this->callback_end();
		else
            this->callback_error(ErrorCode::IO_ERROR, errno);
		//This pointer might be dangeling now because callbacks could delete up!
		return;
	}

	auto callback = is_err_stream ? this->callback_read_error : this->callback_read_output;
	if(callback) callback(buffer, read_buffer_length);
}

void FFMpegProcessHandle::enable_buffering() {
    std::lock_guard io_lock{this->io.lock};

    if(this->buffering) return;
    this->buffering = true;

    /* add the events to read more data */
    if(this->io.event_out) libevent::functions->event_add(this->io.event_out, nullptr);
}

void FFMpegProcessHandle::disable_buffering() {
    std::lock_guard io_lock{this->io.lock};

    if(!this->buffering) return;
    this->buffering = false;

    /* remove the events, we do not want to read more data */
    if(this->io.event_out) libevent::functions->event_del_noblock(this->io.event_out);
}

void FFMpegProcessHandle::schedule_timer(const std::chrono::system_clock::time_point &time_point) {
    using namespace std;
    const auto now = chrono::system_clock::now();
    struct timeval time{0, 1};
    if(now < time_point) {
        auto micros = chrono::floor<chrono::microseconds>(time_point - now);
        auto seconds = chrono::duration_cast<chrono::seconds>(micros);
        micros -= seconds;

        time.tv_usec = micros.count();
        time.tv_sec = seconds.count();
    }

    std::lock_guard io_lock{this->io.lock};
    if(this->io.event_timer)
        libevent::functions->event_add(this->io.event_timer, &time);
}

void FFMpegProcessHandle::cancel_timer() {
    std::lock_guard io_lock{this->io.lock};
    if(this->io.event_timer)
        libevent::functions->event_del_noblock(this->io.event_timer);
}