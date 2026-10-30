#pragma once

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_iostream.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace App::Audio {

/// A QD ADP file (music track or OBJECTS voice-over) exposed as a seekable
/// raw S16 PCM SDL_IOStream.
///
/// Runtime keeps the compressed track in memory (0x0042BFD0 reads the whole
/// file) and decodes it incrementally: a 15 ms multimedia timer (0x0042CC40)
/// refills only the part of an 8-second DirectSound ring the play cursor has
/// consumed. This stream keeps the same split: it owns the compressed bytes
/// and decodes small windows on demand as the mixer reads, so starting
/// playback never decodes the whole file.
struct AdpStream {
  /// Owns its state; close it (or hand it to SDL_mixer with closeio) once.
  SDL_IOStream* io{nullptr};
  SDL_AudioSpec spec{};
  std::uint64_t total_frames{0};
};

/// Opens `file` (a complete .ADP) as a lazily decoded PCM stream.
[[nodiscard]] std::expected<AdpStream, std::string> open_adp_stream(std::vector<std::byte> file);

}  // namespace App::Audio
