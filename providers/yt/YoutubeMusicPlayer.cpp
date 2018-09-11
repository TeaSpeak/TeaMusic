#include "YoutubeMusicPlayer.h"
#include "YTVManager.h"

using namespace music::player;

YoutubeMusicPlayer::YoutubeMusicPlayer(std::shared_ptr<yt::AudioInfo> info) : FFMpegMusicPlayer(info->stream_url, info->live_stream), video(info) {}
YoutubeMusicPlayer::~YoutubeMusicPlayer() {}

std::string music::player::YoutubeMusicPlayer::songTitle() {
    return this->video->title;
}

std::string music::player::YoutubeMusicPlayer::songDescription() {
    return this->video->description;
}

std::deque<std::shared_ptr<music::Thumbnail>> YoutubeMusicPlayer::thumbnails() {
	auto response = FFMpegMusicPlayer::thumbnails();
	if(!this->video->thumbnail.empty())
		response.push_front(std::make_shared<music::ThumbnailUrl>(this->video->thumbnail));
	return response;
}
