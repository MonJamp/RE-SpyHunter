#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include "../Configuration/SpyHunterConfig.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr auto kIntroMovie = "MOVIES/M_LOGO.PSS";

bool extractAudioStream(const std::filesystem::path& source,
                        std::vector<unsigned char>& audio,
                        std::string& error)
{
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        error = "Could not read the PSS movie for audio.";
        return false;
    }
    const std::vector<unsigned char> bytes{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    size_t declaredSize = 0;

    for (size_t offset = 0; offset + 9 < bytes.size();) {
        if (bytes[offset] != 0 || bytes[offset + 1] != 0 || bytes[offset + 2] != 1 ||
            bytes[offset + 3] != 0xBD) {
            ++offset;
            continue;
        }
        const size_t packetLength = (static_cast<size_t>(bytes[offset + 4]) << 8) | bytes[offset + 5];
        const size_t packetEnd = offset + 6 + packetLength;
        size_t payloadStart = offset + 9 + bytes[offset + 8];
        if (packetLength == 0 || payloadStart + 4 > packetEnd || packetEnd > bytes.size()) {
            ++offset;
            continue;
        }

        payloadStart += 4; // FF A0 00 00 private-stream header.
        if (declaredSize == 0) {
            size_t soundDataHeader = payloadStart;
            while (soundDataHeader + 8 <= packetEnd &&
                   !(bytes[soundDataHeader] == 'S' && bytes[soundDataHeader + 1] == 'S' &&
                     bytes[soundDataHeader + 2] == 'b' && bytes[soundDataHeader + 3] == 'd')) {
                ++soundDataHeader;
            }
            if (soundDataHeader + 8 > packetEnd) {
                offset = packetEnd;
                continue;
            }
            declaredSize = static_cast<size_t>(bytes[soundDataHeader + 4]) |
                (static_cast<size_t>(bytes[soundDataHeader + 5]) << 8) |
                (static_cast<size_t>(bytes[soundDataHeader + 6]) << 16) |
                (static_cast<size_t>(bytes[soundDataHeader + 7]) << 24);
            audio.reserve(declaredSize);
            payloadStart = soundDataHeader + 8;
        }

        const size_t remaining = declaredSize - (std::min)(declaredSize, audio.size());
        const size_t copySize = (std::min)(packetEnd - payloadStart, remaining);
        audio.insert(audio.end(), bytes.begin() + payloadStart, bytes.begin() + payloadStart + copySize);
        offset = packetEnd;
        if (audio.size() == declaredSize) break;
    }

    if (declaredSize == 0 || audio.size() != declaredSize) {
        error = "Sony PCM audio extraction was incomplete.";
        return false;
    }
    SDL_Log("Extracted %zu bytes of 48 kHz stereo PCM audio.", audio.size());
    return true;
}

std::string describeHResult(const HRESULT result)
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

    HRESULT result() const
    {
        return FAILED(comResult_) && comResult_ != RPC_E_CHANGED_MODE ? comResult_ : mfResult_;
    }

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
        if (SUCCEEDED(result)) result = MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader_);
        if (FAILED(result)) {
            error = "Media Foundation could not open the movie (" + describeHResult(result) + ")";
            return false;
        }

        reader_->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        reader_->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

        ComPtr<IMFMediaType> outputType;
        result = MFCreateMediaType(&outputType);
        if (SUCCEEDED(result)) result = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(result)) result = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(result)) result = reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType.Get());
        if (FAILED(result)) {
            error = "The movie's video stream could not be decoded (" + describeHResult(result) + ")";
            return false;
        }

        ComPtr<IMFMediaType> negotiatedType;
        result = reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &negotiatedType);
        if (SUCCEEDED(result)) result = MFGetAttributeSize(negotiatedType.Get(), MF_MT_FRAME_SIZE, &width_, &height_);
        if (FAILED(result) || width_ == 0 || height_ == 0) {
            error = "The decoded video has no valid frame size (" + describeHResult(result) + ")";
            return false;
        }
        return true;
    }

    bool readFrame(ComPtr<IMFSample>& sample, LONGLONG& timestamp, bool& finished, std::string& error)
    {
        DWORD flags = 0;
        const HRESULT result = reader_->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &timestamp, &sample);
        if (FAILED(result)) {
            error = "Movie decoding failed (" + describeHResult(result) + ")";
            return false;
        }
        finished = (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0;
        return true;
    }

    UINT32 width() const { return width_; }
    UINT32 height() const { return height_; }

private:
    ComPtr<IMFSourceReader> reader_;
    UINT32 width_ = 0;
    UINT32 height_ = 0;
};

bool renderSample(SDL_Renderer* renderer, SDL_Texture* texture, IMFSample* sample,
                  const UINT32 width, const UINT32 height, std::string& error)
{
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT result = sample->ConvertToContiguousBuffer(&buffer);
    BYTE* pixels = nullptr;
    DWORD byteCount = 0;
    if (SUCCEEDED(result)) result = buffer->Lock(&pixels, nullptr, &byteCount);
    if (FAILED(result)) {
        error = "Decoded frame access failed (" + describeHResult(result) + ")";
        return false;
    }

    const auto expectedBytes = static_cast<DWORD>(width * height * 4);
    const bool validFrame = byteCount >= expectedBytes;
    if (validFrame) SDL_UpdateTexture(texture, nullptr, pixels, static_cast<int>(width * 4));
    buffer->Unlock();
    if (!validFrame) {
        error = "Decoded frame was smaller than its advertised dimensions.";
        return false;
    }

    int outputWidth = 0;
    int outputHeight = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight);
    const float scale = (std::min)(
        static_cast<float>(outputWidth) / static_cast<float>(width),
        static_cast<float>(outputHeight) / static_cast<float>(height));
    const SDL_FRect destination{
        (outputWidth - width * scale) * 0.5f,
        (outputHeight - height * scale) * 0.5f,
        width * scale,
        height * scale};

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, nullptr, &destination);
    SDL_RenderPresent(renderer);
    return true;
}
}

int main(int argc, char* argv[])
{
    const std::filesystem::path configPath = argc > 1
        ? std::filesystem::path(argv[1])
        : spyhunter::configuration::findConfigFile(std::filesystem::current_path());

    const auto configResult = spyhunter::configuration::load(configPath);
    if (!configResult.config) {
        SDL_Log("Configuration error: %s", configResult.error.c_str());
        SDL_Log("Pass the path to SpyHunter.config as the first argument.");
        return 1;
    }

    const auto introMoviePath = configResult.config->extractedBinariesPath / kIntroMovie;
    if (!std::filesystem::is_regular_file(introMoviePath)) {
        SDL_Log("Intro movie was not found: %s", introMoviePath.string().c_str());
        return 1;
    }

    SDL_Log("Configuration: %s", configPath.string().c_str());
    SDL_Log("Playing intro movie: %s", introMoviePath.string().c_str());

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL initialization failed: %s", SDL_GetError());
        return 1;
    }

    MediaFoundation mediaFoundation;
    if (FAILED(mediaFoundation.result())) {
        SDL_Log("Media Foundation initialization failed: %s", describeHResult(mediaFoundation.result()).c_str());
        SDL_Quit();
        return 1;
    }

    std::string error;
    std::vector<unsigned char> audioSamples;
    if (!extractAudioStream(introMoviePath, audioSamples, error)) {
        SDL_Log("%s", error.c_str());
        SDL_Quit();
        return 1;
    }

    const auto generatedDirectory = configPath.parent_path() / "local-assets";
    // The Windows MPEG source resolver needs a recognized container suffix. The
    // PSS itself is an MPEG-2 program stream, so use a generated, renamed copy;
    // its Sony private audio packets are ignored while the video stream is read.
    const auto generatedProgramPath = generatedDirectory / "M_LOGO.mpg";
    std::error_code copyError;
    std::filesystem::create_directories(generatedDirectory, copyError);
    if (copyError) {
        SDL_Log("Could not create the generated-media directory: %s", copyError.message().c_str());
        SDL_Quit();
        return 1;
    }
    std::filesystem::copy_file(
        introMoviePath, generatedProgramPath,
        std::filesystem::copy_options::overwrite_existing, copyError);
    if (copyError) {
        SDL_Log("Could not create the generated MPEG program stream: %s", copyError.message().c_str());
        SDL_Quit();
        return 1;
    }

    MovieReader movie;
    if (!movie.open(generatedProgramPath, error)) {
        SDL_Log("%s", error.c_str());
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(
        "SpyHunter - Startup Player - M_LOGO.PSS", 960, 720,
        SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
        static_cast<int>(movie.width()), static_cast<int>(movie.height()));
    if (!texture) {
        SDL_Log("Texture creation failed: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const SDL_AudioSpec audioSpec{SDL_AUDIO_S16LE, 2, 48000};
    SDL_AudioStream* audioStream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audioSpec, nullptr, nullptr);
    if (!audioStream || !SDL_PutAudioStreamData(
            audioStream, audioSamples.data(), static_cast<int>(audioSamples.size())) ||
        !SDL_ResumeAudioStreamDevice(audioStream)) {
        SDL_Log("Audio playback initialization failed: %s", SDL_GetError());
        if (audioStream) SDL_DestroyAudioStream(audioStream);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Log("Decoded video: %ux%u. Escape closes the player.", movie.width(), movie.height());
    const auto playbackStart = std::chrono::steady_clock::now();
    bool running = true;
    bool finished = false;

    while (running && !finished) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }

        ComPtr<IMFSample> sample;
        LONGLONG timestamp = 0;
        if (!movie.readFrame(sample, timestamp, finished, error)) {
            SDL_Log("%s", error.c_str());
            running = false;
            break;
        }
        if (!sample) continue;

        const auto presentationTime = playbackStart + std::chrono::nanoseconds(timestamp * 100);
        std::this_thread::sleep_until(presentationTime);
        if (!renderSample(renderer, texture, sample.Get(), movie.width(), movie.height(), error)) {
            SDL_Log("%s", error.c_str());
            running = false;
        }
    }

    SDL_DestroyAudioStream(audioStream);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return error.empty() ? 0 : 1;
}
