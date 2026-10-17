#include "CutscenePlayer.h"
#include "PssAudioReader.h"

#include "../Configuration/SpyHunterLog.h"

#include <SDL3/SDL.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace spyhunter::runtime
{
namespace
{
std::string describeHResult(HRESULT result)
{
    char buffer[32]{};
    SDL_snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<unsigned>(result));
    return buffer;
}

class MediaFoundation
{
public:
    MediaFoundation()
    {
        comResult_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(comResult_) || comResult_ == RPC_E_CHANGED_MODE) mfResult_ = MFStartup(MF_VERSION);
    }
    ~MediaFoundation()
    {
        if (SUCCEEDED(mfResult_)) MFShutdown();
        if (SUCCEEDED(comResult_)) CoUninitialize();
    }
    HRESULT result() const { return FAILED(comResult_) && comResult_ != RPC_E_CHANGED_MODE ? comResult_ : mfResult_; }
private:
    HRESULT comResult_ = E_FAIL;
    HRESULT mfResult_ = E_FAIL;
};

class MovieReader
{
public:
    bool open(const std::filesystem::path& path, std::string& error)
    {
        ComPtr<IMFAttributes> attributes;
        HRESULT result = MFCreateAttributes(&attributes, 2);
        if (SUCCEEDED(result)) result = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(result)) result = attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
        if (SUCCEEDED(result)) result = MFCreateFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST,
                                                     MF_FILEFLAGS_NONE, path.c_str(), &byteStream_);
        ComPtr<IMFAttributes> byteStreamAttributes;
        if (SUCCEEDED(result)) result = byteStream_.As(&byteStreamAttributes);
        if (SUCCEEDED(result)) result = byteStreamAttributes->SetString(MF_BYTESTREAM_CONTENT_TYPE, L"video/mpeg");
        if (SUCCEEDED(result)) result = MFCreateSourceReaderFromByteStream(byteStream_.Get(), attributes.Get(), &reader_);
        if (FAILED(result)) { error = "Media Foundation could not open the movie (" + describeHResult(result) + ")"; return false; }

        reader_->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        reader_->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        ComPtr<IMFMediaType> outputType;
        result = MFCreateMediaType(&outputType);
        if (SUCCEEDED(result)) result = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(result)) result = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(result)) result = reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType.Get());
        if (FAILED(result)) { error = "The movie's video stream could not be decoded (" + describeHResult(result) + ")"; return false; }

        ComPtr<IMFMediaType> negotiatedType;
        result = reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &negotiatedType);
        if (SUCCEEDED(result)) result = MFGetAttributeSize(negotiatedType.Get(), MF_MT_FRAME_SIZE, &width_, &height_);
        if (FAILED(result) || width_ == 0 || height_ == 0) { error = "The decoded video has no valid frame size (" + describeHResult(result) + ")"; return false; }
        return true;
    }

    bool readFrame(ComPtr<IMFSample>& sample, LONGLONG& timestamp, bool& finished, std::string& error)
    {
        DWORD flags = 0;
        const HRESULT result = reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &timestamp, &sample);
        if (FAILED(result)) { error = "Movie decoding failed (" + describeHResult(result) + ")"; return false; }
        finished = (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0;
        return true;
    }
    UINT32 width() const { return width_; }
    UINT32 height() const { return height_; }
private:
    ComPtr<IMFByteStream> byteStream_;
    ComPtr<IMFSourceReader> reader_;
    UINT32 width_ = 0;
    UINT32 height_ = 0;
};

bool updateTexture(SDL_Texture* texture, IMFSample* sample, UINT32 width, UINT32 height, std::string& error)
{
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT result = sample->ConvertToContiguousBuffer(&buffer);
    BYTE* pixels = nullptr;
    DWORD byteCount = 0;
    if (SUCCEEDED(result)) result = buffer->Lock(&pixels, nullptr, &byteCount);
    if (FAILED(result)) { error = "Decoded frame access failed (" + describeHResult(result) + ")"; return false; }
    const auto expectedBytes = static_cast<DWORD>(width * height * 4);
    const bool validFrame = byteCount >= expectedBytes;
    if (validFrame) SDL_UpdateTexture(texture, nullptr, pixels, static_cast<int>(width * 4));
    buffer->Unlock();
    if (!validFrame) { error = "Decoded frame was smaller than its advertised dimensions."; return false; }
    return true;
}
}

class CutsceneSession::Implementation
{
public:
    MediaFoundation mediaFoundation;
    MovieReader movie;
    PssAudioReader audioReader;
    SDL_Texture* texture = nullptr;
    SDL_AudioStream* audioStream = nullptr;
    std::vector<std::uint8_t> audioRemainder;
    int maximumQueuedAudioBytes = 0;
    bool reportedAudioUnderrun = false;
    std::chrono::steady_clock::time_point playbackStart;
    ComPtr<IMFSample> pendingSample;
    LONGLONG pendingTimestamp = 0;
    bool hasPendingSample = false;
    bool pumpAudio(const int targetBytes, const std::uint64_t scanBudget, std::string& error)
    {
        int queued = SDL_GetAudioStreamQueued(audioStream);
        if (queued < 0) { error = "Could not query queued audio: " + std::string(SDL_GetError()); return false; }
        if (queued == 0 && !reportedAudioUnderrun && audioReader.audioBytesRead() > 0 &&
            audioReader.audioBytesRead() < audioReader.declaredAudioBytes()) {
            reportedAudioUnderrun = true;
            logging::write("GameRuntime", logging::Level::warning,
                           "audio queue drained before the PSS track ended; source_bytes=" +
                           std::to_string(audioReader.sourceBytesConsumed()) +
                           " audio_bytes_read=" + std::to_string(audioReader.audioBytesRead()));
        }
        const int queuedAtStart = queued;
        const auto scanStart = audioReader.sourceBytesConsumed();
        std::vector<std::uint8_t> audio;
        while (queued < targetBytes && audioReader.sourceBytesConsumed() - scanStart < scanBudget) {
            const auto remainingBudget = scanBudget - (audioReader.sourceBytesConsumed() - scanStart);
            const auto result = audioReader.next(audio, error, remainingBudget);
            if (result == AudioReadResult::failed) return false;
            if (result == AudioReadResult::pending) break;
            if (result == AudioReadResult::complete) {
                if (!audioRemainder.empty()) { error = "PSS audio ends within a stereo interleave block."; return false; }
                break;
            }
            audioRemainder.insert(audioRemainder.end(), audio.begin(), audio.end());
            const auto channelBlock = static_cast<std::size_t>(audioReader.interleaveBytes());
            const auto stereoBlock = channelBlock * 2;
            const auto wholeBlocks = (audioRemainder.size() / stereoBlock) * stereoBlock;
            if (wholeBlocks == 0) continue;
            std::vector<std::uint8_t> interleaved(wholeBlocks);
            for (std::size_t block = 0; block < wholeBlocks; block += stereoBlock) {
                for (std::size_t sample = 0; sample < channelBlock; sample += 2) {
                    const auto target = block + sample * 2;
                    interleaved[target] = audioRemainder[block + sample];
                    interleaved[target + 1] = audioRemainder[block + sample + 1];
                    interleaved[target + 2] = audioRemainder[block + channelBlock + sample];
                    interleaved[target + 3] = audioRemainder[block + channelBlock + sample + 1];
                }
            }
            if (!SDL_PutAudioStreamData(audioStream, interleaved.data(), static_cast<int>(wholeBlocks))) {
                error = "Could not queue PSS audio: " + std::string(SDL_GetError());
                return false;
            }
            queued += static_cast<int>(wholeBlocks);
            maximumQueuedAudioBytes = (std::max)(maximumQueuedAudioBytes, queued);
            audioRemainder.erase(audioRemainder.begin(), audioRemainder.begin() + static_cast<std::ptrdiff_t>(wholeBlocks));
        }
        if (queued > queuedAtStart && logging::enabled("GameRuntime", logging::Level::trace)) {
            logging::write("GameRuntime", logging::Level::trace,
                           "audio_queue added_bytes=" + std::to_string(queued - queuedAtStart) +
                           " queued_bytes=" + std::to_string(queued) +
                           " source_bytes=" + std::to_string(audioReader.sourceBytesConsumed()));
        }
        return true;
    }
    ~Implementation()
    {
        if (audioStream) SDL_DestroyAudioStream(audioStream);
        if (texture) SDL_DestroyTexture(texture);
    }
};

CutsceneSession::CutsceneSession() = default;
CutsceneSession::~CutsceneSession() = default;
CutsceneSession::CutsceneSession(CutsceneSession&&) noexcept = default;
CutsceneSession& CutsceneSession::operator=(CutsceneSession&&) noexcept = default;

bool CutsceneSession::load(SDL_Renderer* renderer, const CutsceneOptions& options, std::string& error)
{
    stop();
    const auto loadStart = std::chrono::steady_clock::now();
    const auto fail = [&](const std::string& message) {
        error_ = error = message;
        logging::write("GameRuntime", logging::Level::error, message);
        return false;
    };
    if (!renderer) return fail("Cutscene playback requires an SDL renderer.");
    if (!std::filesystem::is_regular_file(options.sourcePath)) return fail("Cutscene was not found: " + options.sourcePath.string());
    std::error_code sizeError;
    const auto sourceSize = std::filesystem::file_size(options.sourcePath, sizeError);
    logging::write("GameRuntime", logging::Level::info,
                   "open source=" + options.sourcePath.string() + " file_bytes=" +
                   (sizeError ? "unknown" : std::to_string(sourceSize)));
    auto implementation = std::make_unique<Implementation>();
    if (FAILED(implementation->mediaFoundation.result())) return fail("Media Foundation initialization failed (" + describeHResult(implementation->mediaFoundation.result()) + ")");
    if (!implementation->movie.open(options.sourcePath, error)) return fail(error);
    if (!implementation->audioReader.open(options.sourcePath, error)) return fail(error);
    implementation->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, static_cast<int>(implementation->movie.width()), static_cast<int>(implementation->movie.height()));
    if (!implementation->texture) return fail("Texture creation failed: " + std::string(SDL_GetError()));
    const SDL_AudioSpec audioSpec{SDL_AUDIO_S16LE, 2, 48000};
    implementation->audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audioSpec, nullptr, nullptr);
    if (!implementation->audioStream) return fail("Audio playback initialization failed: " + std::string(SDL_GetError()));
    if (!implementation->pumpAudio(192 * 1024, 8 * 1024 * 1024, error)) return fail(error);
    if (!SDL_ResumeAudioStreamDevice(implementation->audioStream)) return fail("Audio playback initialization failed: " + std::string(SDL_GetError()));
    implementation->playbackStart = std::chrono::steady_clock::now();
    const auto loadMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(implementation->playbackStart - loadStart).count();
    logging::write("GameRuntime", logging::Level::info,
                   "ready elapsed_ms=" + std::to_string(loadMilliseconds) + " video=" +
                   std::to_string(implementation->movie.width()) + "x" + std::to_string(implementation->movie.height()) +
                   " audio_source_bytes=" + std::to_string(implementation->audioReader.sourceBytesConsumed()) +
                   " audio_queued_bytes=" + std::to_string(implementation->maximumQueuedAudioBytes) +
                   " audio_declared_bytes=" + std::to_string(implementation->audioReader.declaredAudioBytes()));
    implementation_ = std::move(implementation);
    finished_ = false;
    error_.clear();
    return true;
}

void CutsceneSession::update()
{
    if (!implementation_ || finished_ || !error_.empty()) return;
    if (!implementation_->pumpAudio(384 * 1024, 2 * 1024 * 1024, error_)) {
        logging::write("GameRuntime", logging::Level::error, error_);
        return;
    }
    if (!implementation_->hasPendingSample) {
        bool endOfStream = false;
        if (!implementation_->movie.readFrame(implementation_->pendingSample, implementation_->pendingTimestamp, endOfStream, error_)) {
            logging::write("GameRuntime", logging::Level::error, error_);
            return;
        }
        if (endOfStream) {
            finished_ = true;
            logging::write("GameRuntime", logging::Level::info, "video_end audio_bytes_read=" + std::to_string(implementation_->audioReader.audioBytesRead()));
            return;
        }
        if (!implementation_->pendingSample) return;
        implementation_->hasPendingSample = true;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - implementation_->playbackStart).count() / 100;
    if (implementation_->pendingTimestamp > elapsed) return;
    if (!updateTexture(implementation_->texture, implementation_->pendingSample.Get(), implementation_->movie.width(), implementation_->movie.height(), error_)) {
        logging::write("GameRuntime", logging::Level::error, error_);
        return;
    }
    implementation_->pendingSample.Reset();
    implementation_->hasPendingSample = false;
    if (logging::enabled("GameRuntime", logging::Level::trace)) {
        logging::write("GameRuntime", logging::Level::trace,
                       "video_frame timestamp_100ns=" + std::to_string(implementation_->pendingTimestamp));
    }
}

void CutsceneSession::render(SDL_Renderer* renderer, const SDL_FRect& viewport) const
{
    if (!implementation_ || !implementation_->texture || viewport.w <= 0 || viewport.h <= 0) return;
    const float scale = (std::min)(viewport.w / static_cast<float>(implementation_->movie.width()), viewport.h / static_cast<float>(implementation_->movie.height()));
    const SDL_FRect destination{viewport.x + (viewport.w - implementation_->movie.width() * scale) * 0.5f, viewport.y + (viewport.h - implementation_->movie.height() * scale) * 0.5f, implementation_->movie.width() * scale, implementation_->movie.height() * scale};
    SDL_RenderTexture(renderer, implementation_->texture, nullptr, &destination);
}

void CutsceneSession::stop()
{
    if (implementation_) {
        logging::write("GameRuntime", logging::Level::debug,
                       "stop audio_source_bytes=" + std::to_string(implementation_->audioReader.sourceBytesConsumed()) +
                       " audio_bytes_read=" + std::to_string(implementation_->audioReader.audioBytesRead()) +
                       " max_audio_queued_bytes=" + std::to_string(implementation_->maximumQueuedAudioBytes));
    }
    implementation_.reset();
    finished_ = false;
    error_.clear();
}

void CutsceneSession::requestSkip()
{
    implementation_.reset();
    error_.clear();
    finished_ = true;
}

bool CutsceneSession::isPlaying() const { return implementation_ && !finished_ && error_.empty(); }
bool CutsceneSession::hasFinished() const { return finished_; }
const std::string& CutsceneSession::error() const { return error_; }

CutscenePlayback playCutscene(SDL_Renderer* renderer, const CutsceneOptions& options)
{
    CutscenePlayback playback;
    CutsceneSession session;
    if (!session.load(renderer, options, playback.error)) return playback;
    bool skipped = false;
    while (session.isPlaying()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || (options.escapeSkips && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) { skipped = true; session.stop(); }
        }
        session.update();
        int width = 0; int height = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        session.render(renderer, SDL_FRect{0, 0, static_cast<float>(width), static_cast<float>(height)});
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }
    if (!session.error().empty()) { playback.error = session.error(); return playback; }
    playback.result = skipped ? CutsceneResult::skipped : CutsceneResult::completed;
    return playback;
}
}
