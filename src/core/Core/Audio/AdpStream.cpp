#include "Core/Audio/AdpStream.hpp"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Omikron/QdAdp.hpp"

namespace App::Audio {

namespace {

/// Frames decoded per window (~0.19 s at 22050 Hz): small enough that a read
/// never stalls the mixer, large enough to amortize per-call overhead.
constexpr std::size_t K_WINDOW_FRAMES{4096};

/// Heap-pinned stream state. The decoder holds a span into `file`, so the
/// state is never moved once the decoder exists.
struct StreamState {
  std::vector<std::byte> file;
  std::unique_ptr<Omikron::QdAdpDecoder> decoder;
  std::size_t frame_bytes{0};
  std::uint64_t total_bytes{0};
  /// Logical read position in bytes of the virtual PCM stream.
  std::uint64_t position{0};
  /// Decoded window: frames [window_start, window_start + window_frames).
  std::vector<std::int16_t> window;
  std::uint64_t window_start{0};
  std::uint64_t window_frames{0};
  std::vector<std::int16_t> skip_scratch;
};

/// Positions the decoder at `frame`; seeking backwards restarts the IMA
/// state from the top, since ADPCM cannot resume mid-stream.
void seek_decoder(StreamState& state, const std::uint64_t frame) {
  Omikron::QdAdpDecoder& decoder{*state.decoder};
  if (frame < decoder.decoded_frames()) {
    decoder.rewind();
  }
  state.skip_scratch.resize(K_WINDOW_FRAMES * static_cast<std::size_t>(decoder.channels()));
  while (decoder.decoded_frames() < frame) {
    const std::uint64_t remaining{frame - decoder.decoded_frames()};
    const std::size_t frames{static_cast<std::size_t>(
        std::min<std::uint64_t>(remaining, static_cast<std::uint64_t>(K_WINDOW_FRAMES)))};
    const std::size_t decoded{decoder.decode_frames(std::span{state.skip_scratch}.first(
        frames * static_cast<std::size_t>(decoder.channels())))};
    if (decoded == 0U) {
      break;
    }
  }
}

/// Makes the window cover `frame`. Sequential playback only ever decodes the
/// next window; seeks (loops, mixer probing) reposition first.
bool load_window(StreamState& state, const std::uint64_t frame) {
  if (frame >= state.window_start && frame < state.window_start + state.window_frames) {
    return true;
  }
  Omikron::QdAdpDecoder& decoder{*state.decoder};
  if (decoder.decoded_frames() != frame) {
    seek_decoder(state, frame);
  }
  state.window.resize(K_WINDOW_FRAMES * static_cast<std::size_t>(decoder.channels()));
  state.window_start = decoder.decoded_frames();
  state.window_frames = decoder.decode_frames(std::span{state.window});
  return state.window_frames != 0U && frame >= state.window_start &&
         frame < state.window_start + state.window_frames;
}

Sint64 SDLCALL stream_size(void* const userdata) {
  return static_cast<Sint64>(static_cast<StreamState*>(userdata)->total_bytes);
}

Sint64 SDLCALL stream_seek(void* const userdata, const Sint64 offset, const SDL_IOWhence whence) {
  StreamState& state{*static_cast<StreamState*>(userdata)};
  Sint64 base{0};
  switch (whence) {
    case SDL_IO_SEEK_SET:
      base = 0;
      break;
    case SDL_IO_SEEK_CUR:
      base = static_cast<Sint64>(state.position);
      break;
    case SDL_IO_SEEK_END:
      base = static_cast<Sint64>(state.total_bytes);
      break;
    default:
      SDL_SetError("invalid ADP stream seek origin");
      return -1;
  }
  const Sint64 target{base + offset};
  if (target < 0) {
    SDL_SetError("ADP stream seek before start");
    return -1;
  }
  state.position = std::min(static_cast<std::uint64_t>(target), state.total_bytes);
  return static_cast<Sint64>(state.position);
}

size_t SDLCALL stream_read(
    void* const userdata, void* const ptr, const size_t size, SDL_IOStatus* const status) {
  StreamState& state{*static_cast<StreamState*>(userdata)};
  const std::span<std::byte> output{static_cast<std::byte*>(ptr), size};
  std::size_t written{0};
  while (written < output.size() && state.position < state.total_bytes) {
    const std::uint64_t frame{state.position / state.frame_bytes};
    if (!load_window(state, frame)) {
      break;
    }
    const std::uint64_t window_offset{(state.position - (state.window_start * state.frame_bytes))};
    const std::uint64_t window_bytes{state.window_frames * state.frame_bytes};
    const std::size_t count{static_cast<std::size_t>(
        std::min<std::uint64_t>(window_bytes - window_offset, output.size() - written))};
    const std::span<const std::byte> source{std::as_bytes(std::span{state.window})};
    std::memcpy(output.subspan(written, count).data(),
        source.subspan(static_cast<std::size_t>(window_offset), count).data(),
        count);
    written += count;
    state.position += count;
  }
  if (written < output.size()) {
    *status = SDL_IO_STATUS_EOF;
  }
  return written;
}

size_t SDLCALL stream_write(void* const /*userdata*/,
    const void* const /*ptr*/,
    const size_t /*size*/,
    SDL_IOStatus* const status) {
  *status = SDL_IO_STATUS_READONLY;
  return 0;
}

bool SDLCALL stream_close(void* const userdata) {
  // Reclaims the state released to SDL in open_adp_stream().
  const std::unique_ptr<StreamState> state{static_cast<StreamState*>(userdata)};
  return true;
}

}  // namespace

std::expected<AdpStream, std::string> open_adp_stream(std::vector<std::byte> file) {
  auto state{std::make_unique<StreamState>()};
  state->file = std::move(file);
  auto decoder{Omikron::QdAdpDecoder::create(std::span<const std::byte>{state->file})};
  if (!decoder) {
    return std::expected<AdpStream, std::string>{std::unexpect, std::move(decoder).error()};
  }
  state->decoder = std::make_unique<Omikron::QdAdpDecoder>(std::move(decoder).value());
  const auto channels{static_cast<std::size_t>(state->decoder->channels())};
  state->frame_bytes = channels * sizeof(std::int16_t);
  state->total_bytes = state->decoder->total_frames() * state->frame_bytes;

  AdpStream stream{.io = nullptr,
      .spec = SDL_AudioSpec{.format = SDL_AUDIO_S16LE,
          .channels = state->decoder->channels(),
          .freq = state->decoder->sample_rate()},
      .total_frames = state->decoder->total_frames()};

  SDL_IOStreamInterface interface{};
  SDL_INIT_INTERFACE(&interface);
  interface.size = stream_size;
  interface.seek = stream_seek;
  interface.read = stream_read;
  interface.write = stream_write;
  interface.close = stream_close;
  // SDL owns the state from here; stream_close() frees it.
  StreamState* const owned_state{state.release()};
  stream.io = SDL_OpenIO(&interface, owned_state);
  if (stream.io == nullptr) {
    const std::string error{SDL_GetError()};
    static_cast<void>(stream_close(owned_state));
    return std::expected<AdpStream, std::string>{
        std::unexpect, "SDL_OpenIO failed for ADP stream: " + error};
  }
  return stream;
}

}  // namespace App::Audio
