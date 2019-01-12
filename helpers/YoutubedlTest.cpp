#include <iostream>
#include <CXXTerminal/Terminal.h>
#include "providers/yt/YTProvider.h"
#include "../../shared/src/log/LogUtils.h"

using namespace std;
int main() {
	terminal::install();
	auto config = make_shared<logger::LoggerConfig>();
	config->terminalLevel = spdlog::level::trace;
	config->logfileLevel = spdlog::level::off;
	logger::setup(config);
	logger::updateLogLevels();

	auto provider = create_provider();
	assert(provider);

	auto info = provider->query_info("https://www.youtube.com/watch?list=PLOHoVaTp8R7d3L_pjuwIa6nRh4tH5nI4x", nullptr, nullptr);
	auto data = static_pointer_cast<music::UrlPlaylistInfo>(info.waitAndGet(nullptr));
	if(info.failed())
		logMessage("Failed message: " + info.errorMegssage());

	__asm__("nop");

	/*
	auto player = provider->createPlayer("https://www.youtube.com/watch?v=Bu3kAbpi5jo").waitAndGet(nullptr);
	assert(player);

	auto thumbnails = player->thumbnails();
	logMessage("Thumbnail length: " + to_string(thumbnails.size()));
	for(const auto& thumbnail : thumbnails) {
		logMessage(" Thumbnail type: " + to_string(thumbnail->type()));
		if(thumbnail->type() == music::THUMBNAIL_URL)
			logMessage(" Thumbnail URL : " + static_pointer_cast<music::ThumbnailUrl>(thumbnail)->url());
	}
	 */
	return 0;
}