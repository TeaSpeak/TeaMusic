#include <include/MusicPlayer.h>
#include "OpusMusicPlayer.h"

using namespace music;
using namespace music::player;
using namespace std;
using namespace std::chrono;

OpusMusicPlayer::OpusMusicPlayer(const std::string& fileName) : file(fileName) {
    this->_preferredSampleCount = 960;
}

OpusMusicPlayer::~OpusMusicPlayer() {}

bool OpusMusicPlayer::initialize() {
    threads::MutexLock lock(this->fileLock);
    int error = 0;

    string file = "./" + this->file;
    this->opusFile = op_open_file(file.c_str(), &error);
    if(error != 0) this->applayError("initialize error code: " + to_string(error) + " file: " + file);
    return this->opusFile && error == 0;
}

void OpusMusicPlayer::play() {
    if(this->reachedEnd){
        op_pcm_seek(this->opusFile, 0);
        this->reachedEnd = false;
    }
    this->readNextSegment();
    AbstractMusicPlayer::play();
}
void OpusMusicPlayer::pause() {
    AbstractMusicPlayer::pause();
}

void OpusMusicPlayer::stop() {
    AbstractMusicPlayer::stop();
    op_pcm_seek(this->opusFile, 0);
    this->reachedEnd = false;
}

void OpusMusicPlayer::forward(const PlayerUnits& duration) {
    threads::MutexLock lock(this->fileLock);
    auto currentOffset = op_pcm_tell(this->opusFile);
    auto offset = this->sampleRate() * duration_cast<milliseconds>(duration).count()  / 1000.d;
    if(op_pcm_total(this->opusFile, -1) <= currentOffset + offset) {
        op_pcm_seek(this->opusFile, op_pcm_total(this->opusFile, -1));
        this->endReached();
        return;
    } else {
        op_pcm_seek(this->opusFile, currentOffset + offset);
    }
    this->readNextSegment();
}

void OpusMusicPlayer::rewind(const PlayerUnits& duration) {
    threads::MutexLock lock(this->fileLock);
    auto currentOffset = op_pcm_tell(this->opusFile);
    auto offset = this->sampleRate() * duration_cast<milliseconds>(duration).count()  / 1000.d;
    if(currentOffset < offset)
        currentOffset = 0;
    else
        currentOffset -= offset;
    op_pcm_seek(this->opusFile, currentOffset);
    this->readNextSegment();
}

PlayerUnits OpusMusicPlayer::length() {
    threads::MutexLock lock(this->fileLock);
    long double channelCount = op_channel_count(this->opusFile, -1);
    long double totalSegments = op_pcm_total(this->opusFile, -1);
    long double bitrate = op_bitrate(this->opusFile, -1);
    return milliseconds((int64_t) (totalSegments / bitrate * channelCount * 1000));
}

PlayerUnits OpusMusicPlayer::currentIndex() {
    threads::MutexLock lock(this->fileLock);
    long double channelCount = op_channel_count(this->opusFile, -1);
    long double currentSegment = op_pcm_tell(this->opusFile);
    long double bitrate = op_bitrate(this->opusFile, -1);
    return milliseconds((int64_t) (currentSegment / bitrate * channelCount * 1000));
}

size_t OpusMusicPlayer::sampleRate() {
    return 48000;
}

std::shared_ptr<music::SampleSegment> OpusMusicPlayer::popNextSegment() {
    threads::MutexLock lock(this->fileLock);
    auto segment = this->nextSegment;
    this->readNextSegment();
    return segment;
}

std::shared_ptr<music::SampleSegment> OpusMusicPlayer::peekNextSegment() {
    threads::MutexLock lock(this->fileLock);
    return this->nextSegment;
}

std::string OpusMusicPlayer::songTitle() {
    return "unknown";
}

std::string OpusMusicPlayer::songDescription() {
    return "unknown";
}

bool OpusMusicPlayer::finished() {
    threads::MutexLock lock(this->fileLock);
    return !this->nextSegment && this->reachedEnd;
}


void OpusMusicPlayer::endReached() {
    this->reachedEnd = true;
    this->fireEvent(music::EVENT_END);
}

void OpusMusicPlayer::readNextSegment() {
    shared_ptr<SampleSegment> segment = std::make_shared<SampleSegment>();
    auto channels = op_channel_count(this->opusFile, -1);
    segment->segments = static_cast<int16_t *>(malloc(this->preferredSampleCount() * channels * sizeof(uint16_t)));
    segment->channels = channels;

    size_t readSegments = 0;
    while(readSegments < this->preferredSampleCount() && this->good()){
        auto read = op_read(this->opusFile, &segment->segments[readSegments * channels], (int) (this->preferredSampleCount() * channels - readSegments * channels), nullptr);
        if(read < 0){
            this->applayError("Invalid opus read (" + to_string(read) + ")");
            break;
        } else if(read == 0){
            this->endReached();
            break;
        }
        readSegments += read;
    }
    segment->segmentLength = readSegments;
    this->nextSegment = segment;
}