#include <iostream>
#include <log/LogUtils.h>
#include <CXXTerminal/Terminal.h>
#include "providers/yt/YTProvider.h"

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

	auto player = provider->createPlayer("https://www.youtube.com/watch?v=Bu3kAbpi5jo").waitAndGet(nullptr);
	assert(player);

	auto thumbnails = player->thumbnails();
	logMessage("Thumbnail length: " + to_string(thumbnails.size()));
	for(const auto& thumbnail : thumbnails) {
		logMessage(" Thumbnail type: " + to_string(thumbnail->type()));
		if(thumbnail->type() == music::THUMBNAIL_URL)
			logMessage(" Thumbnail URL : " + static_pointer_cast<music::ThumbnailUrl>(thumbnail)->url());
	}
}