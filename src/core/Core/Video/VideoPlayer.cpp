#include "Core/Video/VideoPlayer.hpp"

// NOLINTBEGIN(misc-include-cleaner) — the ffmpeg headers do not self-guard for
// C++, so they must be wrapped in extern "C" (which hides them from the
// include-cleaner's symbol mapping).
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
// NOLINTEND(misc-include-cleaner)

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_timer.h>
#include <fmt/format.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Resources.hpp"

namespace App::Video {

namespace {

std::string av_error_message(const int error) {
  std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
  av_strerror(error, buffer.data(), buffer.size());
  return std::string{buffer.data()};
}

}  // namespace

class VideoPlayer::Impl {
 public:
  Impl() {
    av_channel_layout_default(&m_output_channel_layout, K_OUTPUT_CHANNELS);
  }

  ~Impl() {
    close();
    av_channel_layout_uninit(&m_output_channel_layout);
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;

  std::expected<void, std::string> open(const std::string& path, const VideoOpenOptions& options) {
    close();

    const std::filesystem::path root_relative{Resources::game_data_path(path)};
    const std::filesystem::path resolved{Resources::resolve_case_insensitive(root_relative)};

    int result{avformat_open_input(&m_format, resolved.string().c_str(), nullptr, nullptr)};
    if (result < 0) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("cannot open '{}' (resolved '{}'): {}",
              path,
              resolved.string(),
              av_error_message(result))};
    }

    result = avformat_find_stream_info(m_format, nullptr);
    if (result < 0) {
      close();
      return std::expected<void, std::string>{
          std::unexpect, fmt::format("cannot read stream info: {}", av_error_message(result))};
    }

    m_video_stream_index =
        av_find_best_stream(m_format, AVMEDIA_TYPE_VIDEO, -1, -1, &m_video_codec, 0);
    if (m_video_stream_index < 0) {
      close();
      return std::expected<void, std::string>{
          std::unexpect, fmt::format("no video stream in '{}'", path)};
    }

    m_audio_stream_index =
        av_find_best_stream(m_format, AVMEDIA_TYPE_AUDIO, -1, -1, &m_audio_codec, 0);

    if (!open_video_stream()) {
      close();
      return std::expected<void, std::string>{
          std::unexpect, fmt::format("cannot open video decoder for '{}'", path)};
    }
    if (m_audio_stream_index >= 0 && !open_audio_stream()) {
      // Audio is optional: fall back to video-only presentation.
      m_audio_stream_index = -1;
    }

    m_packet = av_packet_alloc();
    m_video_frame = av_frame_alloc();
    m_audio_frame = av_frame_alloc();
    m_rgba_frame = av_frame_alloc();
    if (m_packet == nullptr || m_video_frame == nullptr || m_audio_frame == nullptr ||
        m_rgba_frame == nullptr) {
      close();
      return std::expected<void, std::string>{std::unexpect, "cannot allocate decoder frames"};
    }

    const auto output_result{configure_video_output(options)};
    if (!output_result) {
      const std::string& error{output_result.error()};
      close();
      return std::expected<void, std::string>{std::unexpect, error};
    }

    m_video_time_base = av_q2d(m_format->streams[m_video_stream_index]->time_base);
    return {};
  }

  std::expected<void, std::string> configure_video_output(const VideoOpenOptions& options) {
    m_output_width = m_video_ctx->width;
    m_output_height = m_video_ctx->height;

    if (options.crop.has_value()) {
      const VideoCrop& crop{options.crop.value()};
      const bool invalid_origin{crop.x < 0 || crop.y < 0};
      const bool invalid_size{crop.width <= 0 || crop.height <= 0 ||
                              crop.width > m_video_ctx->width || crop.height > m_video_ctx->height};
      const bool outside_frame{!invalid_size && (crop.x > m_video_ctx->width - crop.width ||
                                                    crop.y > m_video_ctx->height - crop.height)};
      if (invalid_origin || invalid_size || outside_frame) {
        return std::expected<void, std::string>{std::unexpect,
            fmt::format("invalid crop {}x{}+{}+{} for {}x{} video",
                crop.width,
                crop.height,
                crop.x,
                crop.y,
                m_video_ctx->width,
                m_video_ctx->height)};
      }
      m_crop = crop;
      m_output_width = crop.width;
      m_output_height = crop.height;
    }

    m_rgba_frame->format = AV_PIX_FMT_RGBA;
    m_rgba_frame->width = m_output_width;
    m_rgba_frame->height = m_output_height;
    if (av_frame_get_buffer(m_rgba_frame, 0) < 0) {
      return std::expected<void, std::string>{std::unexpect, "cannot allocate RGBA frame buffer"};
    }

    m_sws = sws_getContext(m_output_width,
        m_output_height,
        m_video_ctx->pix_fmt,
        m_output_width,
        m_output_height,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    if (m_sws == nullptr) {
      return std::expected<void, std::string>{std::unexpect, "cannot create scaler"};
    }

    const int* coefficients{sws_getCoefficients(SWS_CS_ITU601)};
    constexpr int k_limited_range{0};
    constexpr int k_full_range{1};
    constexpr int k_neutral_adjustment{1 << 16};
    if (coefficients == nullptr || sws_setColorspaceDetails(m_sws,
                                       coefficients,
                                       k_limited_range,
                                       coefficients,
                                       k_full_range,
                                       /*brightness=*/0,
                                       /*contrast=*/k_neutral_adjustment,
                                       /*saturation=*/k_neutral_adjustment) < 0) {
      return std::expected<void, std::string>{
          std::unexpect, "cannot configure BT.601 video color conversion"};
    }

    return {};
  }

  void start_audio() {
    if (m_audio_stream_index < 0 || m_audio_stream != nullptr) {
      return;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = K_OUTPUT_CHANNELS;
    spec.freq = m_output_sample_rate;
    m_audio_stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (m_audio_stream != nullptr) {
      SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(m_audio_stream));
    }
  }

  void stop_audio() {
    if (m_audio_stream != nullptr) {
      SDL_DestroyAudioStream(m_audio_stream);
      m_audio_stream = nullptr;
    }
  }

  VideoDecodeStatus next_video_frame(VideoFrame& out) {
    APP_PROFILE_SCOPE("VideoDecode");

    if (m_video_stream_index < 0 || m_video_ctx == nullptr) {
      return VideoDecodeStatus::k_error;
    }

    while (true) {
      const int result{av_read_frame(m_format, m_packet)};
      if (result < 0) {
        if (result == AVERROR_EOF) {
          // Flush the video decoder for any buffered frames.
          avcodec_send_packet(m_video_ctx, nullptr);
          if (avcodec_receive_frame(m_video_ctx, m_video_frame) == 0) {
            return convert_video_frame(m_video_frame, out) ? VideoDecodeStatus::k_frame
                                                           : VideoDecodeStatus::k_error;
          }
          return VideoDecodeStatus::k_eof;
        }
        return VideoDecodeStatus::k_error;
      }

      if (m_packet->stream_index == m_video_stream_index) {
        avcodec_send_packet(m_video_ctx, m_packet);
        av_packet_unref(m_packet);
        const int receive{avcodec_receive_frame(m_video_ctx, m_video_frame)};
        if (receive == 0) {
          return convert_video_frame(m_video_frame, out) ? VideoDecodeStatus::k_frame
                                                         : VideoDecodeStatus::k_error;
        }
        if (receive == AVERROR(EAGAIN)) {
          continue;
        }
        return VideoDecodeStatus::k_error;
      }

      if (m_packet->stream_index == m_audio_stream_index && m_audio_stream != nullptr) {
        avcodec_send_packet(m_audio_ctx, m_packet);
        av_packet_unref(m_packet);
        while (avcodec_receive_frame(m_audio_ctx, m_audio_frame) == 0) {
          push_audio_frame(m_audio_frame);
        }
        continue;
      }

      av_packet_unref(m_packet);
    }
  }

  [[nodiscard]] double clock_seconds() const {
    // External clock (ffplay's AV_SYNC_EXTERNAL_CLOCK): a wall clock anchored
    // to the first video frame's PTS. It advances in real time independently
    // of the audio device, so the single-threaded decode/present loop never
    // stalls waiting for audio that it cannot feed while blocked.
    const std::uint64_t now{SDL_GetTicks()};
    return m_wall_origin_pts + (static_cast<double>(now - m_start_ticks) / 1000.0);
  }

  [[nodiscard]] bool has_video() const {
    return m_video_stream_index >= 0;
  }

  [[nodiscard]] bool has_audio() const {
    return m_audio_stream_index >= 0;
  }

  [[nodiscard]] std::int64_t audio_queued_bytes() const {
    if (m_audio_stream == nullptr) {
      return -1;
    }
    return static_cast<std::int64_t>(SDL_GetAudioStreamQueued(m_audio_stream));
  }

 private:
  static constexpr int K_OUTPUT_CHANNELS{2};

  bool open_video_stream() {
    m_video_ctx = avcodec_alloc_context3(m_video_codec);
    if (m_video_ctx == nullptr) {
      return false;
    }
    if (avcodec_parameters_to_context(
            m_video_ctx, m_format->streams[m_video_stream_index]->codecpar) < 0) {
      return false;
    }
    return avcodec_open2(m_video_ctx, m_video_codec, nullptr) >= 0;
  }

  bool open_audio_stream() {
    m_audio_ctx = avcodec_alloc_context3(m_audio_codec);
    if (m_audio_ctx == nullptr) {
      return false;
    }
    if (avcodec_parameters_to_context(
            m_audio_ctx, m_format->streams[m_audio_stream_index]->codecpar) < 0) {
      return false;
    }
    if (avcodec_open2(m_audio_ctx, m_audio_codec, nullptr) < 0) {
      return false;
    }

    m_output_sample_rate = m_audio_ctx->sample_rate > 0 ? m_audio_ctx->sample_rate : 48000;
    if (swr_alloc_set_opts2(&m_swr,
            &m_output_channel_layout,
            AV_SAMPLE_FMT_FLT,
            m_output_sample_rate,
            &m_audio_ctx->ch_layout,
            m_audio_ctx->sample_fmt,
            m_audio_ctx->sample_rate,
            0,
            nullptr) < 0) {
      return false;
    }
    return swr_init(m_swr) >= 0;
  }

  bool convert_video_frame(AVFrame* source, VideoFrame& out) {
    APP_PROFILE_SCOPE("VideoConvert");

    if (m_crop.has_value()) {
      const VideoCrop& crop{m_crop.value()};
      if (crop.width > source->width || crop.height > source->height ||
          crop.x > source->width - crop.width || crop.y > source->height - crop.height) {
        return false;
      }
      source->crop_left = static_cast<std::size_t>(crop.x);
      source->crop_top = static_cast<std::size_t>(crop.y);
      source->crop_right = static_cast<std::size_t>(source->width - crop.x - crop.width);
      source->crop_bottom = static_cast<std::size_t>(source->height - crop.y - crop.height);
      if (av_frame_apply_cropping(source, 0) < 0) {
        return false;
      }
    }

    if (source->width != m_output_width || source->height != m_output_height) {
      return false;
    }

    const int scaled_height{sws_scale(m_sws,
        source->data,
        source->linesize,
        0,
        source->height,
        m_rgba_frame->data,
        m_rgba_frame->linesize)};
    if (scaled_height != m_output_height) {
      return false;
    }

    out.width = m_output_width;
    out.height = m_output_height;
    out.rgba.resize(
        static_cast<std::size_t>(m_output_width) * static_cast<std::size_t>(m_output_height) * 4U);

    const int stride{m_rgba_frame->linesize[0]};
    const std::uint8_t* source_data{m_rgba_frame->data[0]};
    std::uint8_t* destination{out.rgba.data()};
    const std::size_t row_bytes{static_cast<std::size_t>(m_output_width) * 4U};
    // sws_scale produces top-down rows; the codebase's texture convention is
    // bottom-up (row 0 = bottom, see BmpImageDecoder), so flip vertically.
    for (int row{0}; row < m_output_height; ++row) {
      const int source_row{m_output_height - row - 1};
      std::memcpy(destination + (static_cast<std::size_t>(row) * row_bytes),
          source_data + (static_cast<std::size_t>(source_row) * static_cast<std::size_t>(stride)),
          row_bytes);
    }

    const std::int64_t pts{source->best_effort_timestamp};
    out.pts_seconds = pts == AV_NOPTS_VALUE ? 0.0 : static_cast<double>(pts) * m_video_time_base;

    if (!m_wall_clock_ready) {
      m_wall_origin_pts = out.pts_seconds;
      m_start_ticks = SDL_GetTicks();
      m_wall_clock_ready = true;
    }
    return true;
  }

  void push_audio_frame(const AVFrame* frame) {
    if (m_swr == nullptr || m_audio_stream == nullptr) {
      return;
    }
    const int output_samples{swr_get_out_samples(m_swr, frame->nb_samples)};
    if (output_samples <= 0) {
      return;
    }
    const int bytes_per_sample{av_get_bytes_per_sample(AV_SAMPLE_FMT_FLT) * K_OUTPUT_CHANNELS};
    const std::size_t buffer_size{
        static_cast<std::size_t>(output_samples) * static_cast<std::size_t>(bytes_per_sample)};
    std::vector<std::uint8_t> buffer(buffer_size);
    std::uint8_t* output_pointer{buffer.data()};
    const int converted{swr_convert(m_swr,
        &output_pointer,
        output_samples,
        const_cast<const std::uint8_t**>(frame->extended_data),
        frame->nb_samples)};
    if (converted > 0) {
      const int bytes{converted * bytes_per_sample};
      SDL_PutAudioStreamData(m_audio_stream, buffer.data(), bytes);
    }
  }

  void close() {
    stop_audio();
    swr_free(&m_swr);
    sws_freeContext(m_sws);
    m_sws = nullptr;
    av_frame_free(&m_rgba_frame);
    av_frame_free(&m_video_frame);
    av_frame_free(&m_audio_frame);
    av_packet_free(&m_packet);
    avcodec_free_context(&m_video_ctx);
    avcodec_free_context(&m_audio_ctx);
    avformat_close_input(&m_format);
    m_video_codec = nullptr;
    m_audio_codec = nullptr;
    m_video_stream_index = -1;
    m_audio_stream_index = -1;
    m_crop.reset();
    m_output_width = 0;
    m_output_height = 0;
    m_wall_clock_ready = false;
    m_wall_origin_pts = 0.0;
    m_start_ticks = 0;
  }

  AVFormatContext* m_format{nullptr};
  const AVCodec* m_video_codec{nullptr};
  const AVCodec* m_audio_codec{nullptr};
  AVCodecContext* m_video_ctx{nullptr};
  AVCodecContext* m_audio_ctx{nullptr};
  int m_video_stream_index{-1};
  int m_audio_stream_index{-1};
  std::optional<VideoCrop> m_crop;
  int m_output_width{0};
  int m_output_height{0};
  SwsContext* m_sws{nullptr};
  SwrContext* m_swr{nullptr};
  AVPacket* m_packet{nullptr};
  AVFrame* m_video_frame{nullptr};
  AVFrame* m_audio_frame{nullptr};
  AVFrame* m_rgba_frame{nullptr};
  SDL_AudioStream* m_audio_stream{nullptr};
  AVChannelLayout m_output_channel_layout{};
  int m_output_sample_rate{48000};
  double m_video_time_base{0.0};
  double m_wall_origin_pts{0.0};
  bool m_wall_clock_ready{false};
  std::uint64_t m_start_ticks{0};
};

VideoPlayer::VideoPlayer() : m_impl{std::make_unique<Impl>()} {}

VideoPlayer::~VideoPlayer() = default;

std::expected<void, std::string> VideoPlayer::open(
    const std::string& path, const VideoOpenOptions& options) {
  return m_impl->open(path, options);
}

void VideoPlayer::start_audio() {
  m_impl->start_audio();
}

void VideoPlayer::stop_audio() {
  m_impl->stop_audio();
}

VideoDecodeStatus VideoPlayer::next_video_frame(VideoFrame& frame) {
  return m_impl->next_video_frame(frame);
}

double VideoPlayer::clock_seconds() const {
  return m_impl->clock_seconds();
}

bool VideoPlayer::has_video() const {
  return m_impl->has_video();
}

bool VideoPlayer::has_audio() const {
  return m_impl->has_audio();
}

std::int64_t VideoPlayer::audio_queued_bytes() const {
  return m_impl->audio_queued_bytes();
}

}  // namespace App::Video
