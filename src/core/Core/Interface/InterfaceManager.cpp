#include "Core/Interface/InterfaceManager.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Audio/AudioTypes.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/DisplayConfiguration.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/Input/InputAction.hpp"
#include "Core/Input/InputManager.hpp"
#include "Core/Input/InputSource.hpp"
#include "Core/Interface/I2DBumpBackground.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Interface/I2DRenderer.hpp"
#include "Core/Interface/I2DStateTransition.hpp"
#include "Core/Interface/InterfaceDescriptor.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Interface/InterfacePresentation.hpp"
#include "Core/Interface/OptionsMenuLayout.hpp"
#include "Core/Interface/RuntimeText.hpp"
#include "Core/Interface/StartMenuLayout.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/BmpImage.hpp"
#include "Core/Omikron/IamStringTable.hpp"
#include "Core/Texture.hpp"
#include "Core/Window.hpp"
#include "Settings/GameSettings.hpp"

namespace App::Interface {

namespace {

constexpr std::string_view K_BITMAP_DIRECTORY{"I2D/bitmaps"};
constexpr std::string_view K_STRING_TABLE_DIRECTORY{"IAM"};

/// OpenNomad-only START MENU presentation policy. The splash already reaches
/// black before interface 29 opens, so revealing the menu from black continues
/// the established startup visual language without changing AREA timing.
constexpr InterfaceFadePresentationHint K_START_MENU_ENTER_FADE{.color = {0.0F, 0.0F, 0.0F},
    .duration_seconds = 0.50F,
    .easing = InterfacePresentationEasing::k_quadratic_in};

/// New Game keeps the confirmed menu visible for a short commit beat, then
/// fades to full white. Only once opaque is Runtime result 3 delivered; AREA's
/// native 0x77 white->transparent fade then takes over unchanged.
constexpr std::array<InterfaceCompletionPresentationHint, 1> K_START_MENU_COMPLETION_TRANSITIONS{
    InterfaceCompletionPresentationHint{.result = 3,
        .pre_delay_seconds = 0.25F,
        .fade = InterfaceFadePresentationHint{.color = {1.0F, 1.0F, 1.0F},
            .duration_seconds = 0.25F,
            .easing = InterfacePresentationEasing::k_quadratic_in}}};

void initialize_start_menu(InterfaceManager& manager, InterfaceInstance& instance);
void destroy_start_menu(InterfaceManager& manager, InterfaceInstance& instance);
void initialize_options(InterfaceManager& manager, InterfaceInstance& instance);
void destroy_options(InterfaceManager& manager, InterfaceInstance& instance);
void seed_video_settings(InterfaceManager& manager, const InterfaceInstance& instance);

/// Builds the Runtime-formatted resource path for a descriptor field.
std::string bitmap_path(const std::string_view name) {
  std::string path{K_BITMAP_DIRECTORY};
  path += '/';
  path += name;
  return path;
}

std::string string_table_path(const std::string_view name) {
  std::string path{K_STRING_TABLE_DIRECTORY};
  path += '/';
  path += name;
  return path;
}

/// Reads a whole file through the case-insensitive game-data resolver.
std::expected<std::vector<std::byte>, std::string> read_file(const std::string& relative_path) {
  APP_PROFILE_FUNCTION();

  auto loaded{load_game_file(relative_path)};
  if (!loaded) {
    return std::expected<std::vector<std::byte>, std::string>{
        std::unexpect, std::move(loaded).error()};
  }
  return std::move(loaded->bytes);
}

const InterfaceCompletionPresentationHint* completion_transition_for(
    const InterfaceInstance& instance, const std::int16_t result) {
  if (instance.descriptor == nullptr) {
    return nullptr;
  }
  for (const InterfaceCompletionPresentationHint& hint :
      instance.descriptor->presentation_hints.completion_transitions) {
    if (hint.result == result) {
      return &hint;
    }
  }
  return nullptr;
}

bool presentation_input_locked(const InterfaceInstance& instance) {
  return instance.presentation.phase == InterfacePresentationPhase::k_completion ||
         instance.presentation.phase == InterfacePresentationPhase::k_completion_queued;
}

std::string_view text_label(const InterfaceInstance& instance, const I2DTextElement& text) {
  if (!text.literal_text.empty()) {
    return std::string_view{text.literal_text};
  }
  return instance.strings.at(text.string_index);
}
}  // namespace

const InterfaceDescriptor* descriptor_for_id(const std::int32_t id) {
  // Static registry mirroring Runtime's interface-descriptor table.
  // Descriptor #29 is at 0x004CC0AC; descriptor #35 is at 0x004CC2D4.
  static const std::vector<InterfaceDescriptor> k_descriptors{
      InterfaceDescriptor{.id = 28,
          .name = "DIVERS",
          .bitmap_name = "",
          .string_table_name = "",
          .companion_interface = std::nullopt,
          .sounds = std::nullopt,
          .init = nullptr,
          .destroy = nullptr,
          .runtime_flags = 0U,
          .presentation_hints = InterfacePresentationHints{}},
      InterfaceDescriptor{.id = 29,
          .name = "OMK START MENU",
          .bitmap_name = "gfxint.bmp",
          .string_table_name = "Menu",
          .companion_interface = 35,
          .sounds = InterfaceSoundSet{.navigate = "I2D/SOUNDS/men001.wav",
              .confirm = "I2D/SOUNDS/men002.wav",
              .cancel = "I2D/SOUNDS/men003.wav"},
          .init = initialize_start_menu,
          .destroy = destroy_start_menu,
          .runtime_flags = 0x20000400,
          .presentation_hints = InterfacePresentationHints{.enter_fade = K_START_MENU_ENTER_FADE,
              .completion_transitions =
                  std::span<const InterfaceCompletionPresentationHint>{
                      K_START_MENU_COMPLETION_TRANSITIONS}}},
      InterfaceDescriptor{.id = 35,
          .name = "OPTIONS",
          .bitmap_name = "",
          .string_table_name = "Options",
          .companion_interface = std::nullopt,
          .sounds = InterfaceSoundSet{.navigate = "I2D/SOUNDS/SNK001.wav",
              .confirm = "I2D/SOUNDS/SNK002.wav",
              .cancel = "I2D/SOUNDS/SNK003.wav"},
          .init = initialize_options,
          .destroy = destroy_options,
          .runtime_flags = 0x00000000U,
          .presentation_hints = InterfacePresentationHints{}},
  };
  for (const InterfaceDescriptor& descriptor : k_descriptors) {
    if (descriptor.id == id) {
      return &descriptor;
    }
  }
  return nullptr;
}

void commit_transition_destinations(const std::span<const TransitionStateDestination> destinations,
    const std::function<void()>& after_commit) {
  for (const TransitionStateDestination& destination : destinations) {
    if (destination.instance == nullptr || destination.state == nullptr) {
      continue;
    }
    const bool owns_state{std::ranges::any_of(destination.instance->states,
        [state = destination.state](const std::unique_ptr<I2DState>& owned) {
          return owned.get() == state;
        })};
    if (!owns_state) {
      App::Log::warn(LogCategory::Interface,
          "ignored transition destination state not owned by interface {} generation {}",
          destination.instance->handle.interface_id,
          destination.instance->handle.generation);
      continue;
    }
    destination.instance->current_state = destination.state;
  }
  if (after_commit) {
    after_commit();
  }
}

InterfaceManager::InterfaceManager(App::Settings::GameSettings& game_settings)
    : m_game_settings(&game_settings) {
  m_settings_listener_id =
      m_game_settings->add_change_listener([this](const std::string_view stable_id) {
        apply_game_setting(stable_id);
      });
  apply_game_setting("enhancements.animation_interpolation");
  apply_game_setting("enhancements.menu_transition_style");
}

InterfaceManager::~InterfaceManager() {
  m_game_settings->remove_change_listener(m_settings_listener_id);
  close();
}

std::expected<InterfaceHandle, std::string> InterfaceManager::open(
    const InterfaceOpenRequest& request) {
  APP_PROFILE_FUNCTION();

  const std::uint16_t interface_id{request.interface_id};
  const InterfaceDescriptor* descriptor{descriptor_for_id(interface_id)};
  if (descriptor == nullptr) {
    return std::expected<InterfaceHandle, std::string>{
        std::unexpect, fmt::format("interface {} is unsupported", interface_id)};
  }

  App::Log::debug(
      LogCategory::I2D, "opening interface {} \"{}\"", descriptor->id, descriptor->name);

  if (!m_renderer) {
    m_renderer = std::make_unique<I2DRenderer>();
    if (auto result{m_renderer->initialize()}; !result) {
      m_renderer.reset();
      return std::expected<InterfaceHandle, std::string>{
          std::unexpect, fmt::format("renderer: {}", result.error())};
    }
  }

  // Build the instance on the stack first: a failure must not leave a
  // half-constructed instance in the resident set.
  auto instance{std::make_unique<InterfaceInstance>()};
  instance->descriptor = descriptor;
  instance->open_request = request;

  // Descriptor bitmap resource (I2D/bitmaps/<name>).
  if (!descriptor->bitmap_name.empty()) {
    const std::string path{bitmap_path(descriptor->bitmap_name)};
    auto file{read_file(path)};
    if (!file) {
      return std::expected<InterfaceHandle, std::string>{
          std::unexpect, fmt::format("bitmap: {}", file.error())};
    }
    auto bmp{Omikron::BmpImageDecoder::load(std::span<const std::byte>{file.value()})};
    if (!bmp) {
      return std::expected<InterfaceHandle, std::string>{
          std::unexpect, fmt::format("bitmap: {}", bmp.error())};
    }
    auto texture{Texture2D::create(bmp->width,
        bmp->height,
        std::span<const std::uint8_t>{bmp->rgba8},
        k_legacy_effect_texture_policy.encoding,
        k_legacy_effect_texture_policy.filter)};
    if (!texture) {
      return std::expected<InterfaceHandle, std::string>{
          std::unexpect, fmt::format("bitmap: {}", texture.error())};
    }
    instance->bitmap.emplace(std::move(texture).value());
    App::Log::debug(LogCategory::I2D, "bitmap: {} ({}x{})", path, bmp->width, bmp->height);
  }

  // Descriptor string-table resource (IAM/<name>).
  if (!descriptor->string_table_name.empty()) {
    const std::string path{string_table_path(descriptor->string_table_name)};
    auto file{read_file(path)};
    if (!file) {
      return std::expected<InterfaceHandle, std::string>{
          std::unexpect, fmt::format("strings: {}", file.error())};
    }
    auto table{Omikron::IamStringTable::load(std::span<const std::byte>{file.value()})};
    if (!table) {
      return std::expected<InterfaceHandle, std::string>{
          std::unexpect, fmt::format("strings: {}", table.error())};
    }
    const std::size_t entry_count{table->size()};
    instance->strings = std::move(table).value();
    App::Log::debug(LogCategory::I2D, "strings: {} ({} entries)", path, entry_count);
  }

  // Assign the handle before the descriptor init so interface-specific state
  // (e.g. a completion action) can address this instance.
  ++m_generation;
  instance->handle = InterfaceHandle{.interface_id = interface_id, .generation = m_generation};

  // Resident set before init: the init callback may reference the instance
  // through create_state(); it must already be reachable.
  m_instances.push_back(std::move(instance));
  InterfaceInstance& resident{*m_instances.back()};

  // Interface-specific initialization builds the I2D state graph on top of
  // the now-loaded resources.
  if (descriptor->init != nullptr) {
    descriptor->init(*this, resident);
  }

  if (resident.root_state == nullptr || resident.current_state == nullptr) {
    const std::string error{"interface initializer did not establish a root state"};
    App::Log::error(LogCategory::I2D, "{}", error);
    m_instances.pop_back();
    return std::expected<InterfaceHandle, std::string>{std::unexpect, error};
  }

  if (const auto& enter_fade{descriptor->presentation_hints.enter_fade};
      enter_fade.has_value() && enter_fade->duration_seconds > 0.0F) {
    resident.presentation.phase = InterfacePresentationPhase::k_enter_fade;
    resident.presentation.elapsed_seconds = 0.0F;
    resident.presentation.overlay =
        InterfacePresentationOverlay{.color = enter_fade->color, .alpha = 1.0F};
  }

  const InterfaceHandle handle{resident.handle};
  m_focused_interface = handle;
  App::Log::info(LogCategory::Interface,
      "opened {} \"{}\" — handle={}:{}",
      handle.interface_id,
      descriptor->name,
      handle.interface_id,
      handle.generation);
  return handle;
}

void InterfaceManager::close() {
  APP_PROFILE_FUNCTION();

  m_active_transition.reset();
  while (!m_instances.empty()) {
    auto instance{std::move(m_instances.back())};
    m_instances.pop_back();
    destroy_instance(*instance);
  }
  m_focused_interface.reset();
  m_completion_overlay_latch.reset();
}

void InterfaceManager::close(const InterfaceHandle handle) {
  APP_PROFILE_FUNCTION();

  for (auto it{m_instances.begin()}; it != m_instances.end(); ++it) {
    if ((*it)->handle == handle) {
      if (m_active_transition.has_value()) {
        const auto participates = [handle](const TransitionLayer& layer) {
          return layer.interface_handle == handle;
        };
        const bool involved{std::ranges::any_of(m_active_transition->outgoing, participates) ||
                            std::ranges::any_of(m_active_transition->incoming, participates)};
        if (involved) {
          m_active_transition.reset();
        }
      }
      destroy_instance(**it);
      m_instances.erase(it);

      // Re-focus the most recently opened remaining instance (or clear).
      if (m_focused_interface.has_value() && m_focused_interface.value() == handle) {
        m_focused_interface.reset();
        if (!m_instances.empty()) {
          m_focused_interface = m_instances.back()->handle;
        }
      }
      return;
    }
  }
  // Stale handle: harmless no-op.
}

void InterfaceManager::destroy_instance(InterfaceInstance& instance) {
  const InterfaceDescriptor* descriptor{instance.descriptor};
  if (descriptor != nullptr && descriptor->destroy != nullptr) {
    descriptor->destroy(*this, instance);
  }
  App::Log::trace(LogCategory::I2D,
      "closing interface {} \"{}\"",
      descriptor != nullptr ? descriptor->id : -1,
      descriptor != nullptr ? descriptor->name : "?");
  App::Log::trace(LogCategory::Interface,
      "closed handle {}:{}",
      instance.handle.interface_id,
      instance.handle.generation);
}

bool InterfaceManager::contains(const InterfaceHandle handle) const {
  return find(handle) != nullptr;
}

InterfaceInstance* InterfaceManager::find(const InterfaceHandle handle) {
  for (const auto& instance : m_instances) {
    if (instance->handle == handle) {
      return instance.get();
    }
  }
  return nullptr;
}

const InterfaceInstance* InterfaceManager::find(const InterfaceHandle handle) const {
  for (const auto& instance : m_instances) {
    if (instance->handle == handle) {
      return instance.get();
    }
  }
  return nullptr;
}

void InterfaceManager::set_focused(const InterfaceHandle handle) {
  if (contains(handle)) {
    m_focused_interface = handle;
  }
}

const InterfaceInstance* InterfaceManager::instance_at(const std::size_t index) const {
  if (index >= m_instances.size()) {
    return nullptr;
  }
  return m_instances.at(index).get();
}

const InterfaceInstance* InterfaceManager::focused_instance() const {
  if (!m_focused_interface.has_value()) {
    return nullptr;
  }
  return find(m_focused_interface.value());
}

InterfaceInstance* InterfaceManager::focused_instance_mut() {
  if (!m_focused_interface.has_value()) {
    return nullptr;
  }
  return find(m_focused_interface.value());
}

void InterfaceManager::request_completion(const InterfaceHandle handle, const std::int16_t result) {
  InterfaceInstance* instance{find(handle)};
  if (instance == nullptr) {
    return;
  }
  if (presentation_input_locked(*instance)) {
    return;
  }

  const InterfaceCompletionPresentationHint* transition{
      completion_transition_for(*instance, result)};
  if (transition == nullptr ||
      (transition->pre_delay_seconds <= 0.0F && transition->fade.duration_seconds <= 0.0F)) {
    queue_completion(InterfaceCompletion{.handle = handle, .result = result});
    return;
  }

  instance->presentation.phase = InterfacePresentationPhase::k_completion;
  instance->presentation.elapsed_seconds = 0.0F;
  instance->presentation.overlay =
      InterfacePresentationOverlay{.color = transition->fade.color, .alpha = 0.0F};
  instance->presentation.completion_hint = transition;
  instance->presentation.pending_completion =
      InterfaceCompletion{.handle = handle, .result = result};

  App::Log::debug(LogCategory::Interface,
      "interface {} completion {} — presentation hold {:.0f} ms, fade {:.0f} ms",
      handle.interface_id,
      result,
      transition->pre_delay_seconds * 1000.0F,
      transition->fade.duration_seconds * 1000.0F);
}

void InterfaceManager::queue_completion(const InterfaceCompletion& completion) {
  m_completions.push_back(completion);
  const InterfaceInstance* instance{find(completion.handle)};
  App::Log::info(LogCategory::Interface,
      "completed {} \"{}\" — handle={}:{} result={}",
      completion.handle.interface_id,
      instance != nullptr && instance->descriptor != nullptr ? instance->descriptor->name : "?",
      completion.handle.interface_id,
      completion.handle.generation,
      completion.result);
}

std::optional<InterfaceCompletion> InterfaceManager::take_completion() {
  if (m_completions.empty()) {
    return std::nullopt;
  }
  InterfaceCompletion completion{m_completions.front()};
  m_completions.pop_front();
  return completion;
}

std::optional<InterfacePresentationOverlay> InterfaceManager::presentation_overlay() const {
  if (m_completion_overlay_latch.has_value()) {
    return m_completion_overlay_latch;
  }
  for (const auto& instance : std::ranges::reverse_view{m_instances}) {
    if (instance->presentation.overlay.alpha > 0.0F) {
      return instance->presentation.overlay;
    }
  }
  return std::nullopt;
}

void InterfaceManager::update_presentation(InterfaceInstance& instance, const float delta_time) {
  InterfacePresentationState& state{instance.presentation};
  const float delta{std::max(delta_time, 0.0F)};

  if (state.phase == InterfacePresentationPhase::k_enter_fade) {
    if (instance.descriptor == nullptr ||
        !instance.descriptor->presentation_hints.enter_fade.has_value()) {
      state = InterfacePresentationState{};
      return;
    }
    const InterfaceFadePresentationHint& hint{
        instance.descriptor->presentation_hints.enter_fade.value()};
    state.elapsed_seconds += delta;
    const float duration{std::max(hint.duration_seconds, 0.0F)};
    const float progress{
        duration > 0.0F ? std::clamp(state.elapsed_seconds / duration, 0.0F, 1.0F) : 1.0F};
    state.overlay.color = hint.color;
    state.overlay.alpha = 1.0F - evaluate_presentation_easing(hint.easing, progress);
    if (progress >= 1.0F) {
      state = InterfacePresentationState{};
    }
    return;
  }

  if (state.phase != InterfacePresentationPhase::k_completion) {
    return;
  }
  if (state.completion_hint == nullptr || !state.pending_completion.has_value()) {
    state = InterfacePresentationState{};
    return;
  }

  const InterfaceCompletionPresentationHint& hint{*state.completion_hint};
  state.elapsed_seconds += delta;
  state.overlay.color = hint.fade.color;

  const float pre_delay{std::max(hint.pre_delay_seconds, 0.0F)};
  if (state.elapsed_seconds < pre_delay) {
    state.overlay.alpha = 0.0F;
    return;
  }

  const float fade_duration{std::max(hint.fade.duration_seconds, 0.0F)};
  const float fade_elapsed{state.elapsed_seconds - pre_delay};
  const float progress{
      fade_duration > 0.0F ? std::clamp(fade_elapsed / fade_duration, 0.0F, 1.0F) : 1.0F};
  state.overlay.alpha = evaluate_presentation_easing(hint.fade.easing, progress);
  if (progress < 1.0F) {
    return;
  }

  // Application drains completions and closes the instance before this frame
  // renders. Latch the final colour independently so the opaque handoff is
  // still presented. New Game's latch coincides with native 0x77 alpha=1.
  m_completion_overlay_latch =
      InterfacePresentationOverlay{.color = hint.fade.color, .alpha = 1.0F};
  const InterfaceCompletion completion{state.pending_completion.value()};
  state.phase = InterfacePresentationPhase::k_completion_queued;
  state.overlay.alpha = 1.0F;
  queue_completion(completion);
}

void InterfaceManager::update(const float delta_time, const Input::InputManager& input) {
  APP_PROFILE_FUNCTION();

  update_without_input(delta_time);
  handle_navigation(input);
}

void InterfaceManager::update_without_input(const float delta_time) {
  APP_PROFILE_FUNCTION();

  // A completion overlay latched by the previous update only needs to survive
  // that update's render. A newly completed transition below may latch again.
  m_completion_overlay_latch.reset();

  for (const auto& instance : m_instances) {
    if (instance->current_state != nullptr && instance->current_state->background != nullptr) {
      instance->current_state->background->update(delta_time);
    }
    update_presentation(*instance, delta_time);
  }
  update_state_transition(delta_time);
}

void InterfaceManager::render(const int pixel_width, const int pixel_height) {
  APP_PROFILE_FUNCTION();

  if (m_renderer == nullptr) {
    return;
  }
  Debug::I2DCounters counters;
  if (m_active_transition.has_value()) {
    const ActiveStateTransition& transition{m_active_transition.value()};
    const float progress{
        std::clamp(transition.elapsed_seconds / transition.duration_seconds, 0.0F, 1.0F)};
    const I2DTransitionSample sample{
        sample_transition(transition.style, transition.direction, transition.context, progress)};
    const auto is_participant = [&transition](const InterfaceHandle handle) {
      const auto matches = [handle](const TransitionLayer& layer) {
        return layer.interface_handle == handle;
      };
      return std::ranges::any_of(transition.outgoing, matches) ||
             std::ranges::any_of(transition.incoming, matches);
    };

    std::size_t first_participant{m_instances.size()};
    std::size_t last_participant{0U};
    for (std::size_t index{0U}; index < m_instances.size(); ++index) {
      if (is_participant(m_instances.at(index)->handle)) {
        first_participant = std::min(first_participant, index);
        last_participant = index;
      }
    }
    if (first_participant < m_instances.size()) {
      const bool participants_contiguous{
          std::ranges::all_of(std::views::iota(first_participant, last_participant + 1U),
              [this, &is_participant](const std::size_t index) {
                return is_participant(m_instances.at(index)->handle);
              })};
      if (!participants_contiguous) {
        App::Log::error(LogCategory::Interface,
            "transition participants are not contiguous in resident presentation order");
      }
    }

    bool background_rendered{false};
    for (std::size_t index{0U}; index < first_participant; ++index) {
      const auto& instance{m_instances.at(index)};
      m_renderer->render(*instance, m_fonts, pixel_width, pixel_height, counters);
      background_rendered =
          background_rendered ||
          (instance->current_state != nullptr && instance->current_state->background != nullptr);
    }

    const auto render_layers = [this, pixel_width, pixel_height, &counters, &background_rendered](
                                   const std::vector<TransitionLayer>& layers,
                                   const I2DStateVisual& visual) {
      for (const TransitionLayer& layer : layers) {
        if (const InterfaceInstance* instance{find(layer.interface_handle)};
            instance != nullptr && layer.state != nullptr) {
          const bool render_background{!background_rendered && layer.state->background != nullptr};
          m_renderer->render_state(*instance,
              *layer.state,
              visual,
              m_fonts,
              pixel_width,
              pixel_height,
              counters,
              render_background);
          background_rendered = background_rendered || render_background;
        }
      }
    };
    render_layers(transition.outgoing, sample.outgoing);
    render_layers(transition.incoming, sample.incoming);

    const std::size_t after_participants{
        first_participant < m_instances.size() ? last_participant + 1U : m_instances.size()};
    for (std::size_t index{after_participants}; index < m_instances.size(); ++index) {
      const auto& instance{m_instances.at(index)};
      if (!is_participant(instance->handle)) {
        m_renderer->render(*instance, m_fonts, pixel_width, pixel_height, counters);
      }
    }
  } else {
    for (const auto& instance : m_instances) {
      m_renderer->render(*instance, m_fonts, pixel_width, pixel_height, counters);
    }
  }
  if (const auto overlay{presentation_overlay()}; overlay.has_value() && overlay->alpha > 0.0F) {
    m_renderer->render_overlay(overlay->color, overlay->alpha, pixel_width, pixel_height);
    counters.draw_calls += 1U;
  }
  Debug::Metrics::get().set_i2d_counters(counters);
}

float InterfaceManager::render_dialog(const Dialog::DialogPresentation& dialog,
    const std::size_t selected_choice,
    const float scroll_offset,
    const int pixel_width,
    const int pixel_height) {
  if (m_renderer == nullptr) {
    return 0.0F;
  }
  Debug::I2DCounters counters;
  return m_renderer->render_dialog(
      dialog, selected_choice, scroll_offset, m_fonts, pixel_width, pixel_height, counters);
}

void InterfaceManager::render_world_text(const RuntimeTextDocument& document,
    const std::uint64_t presentation_time_ms,
    const int pixel_width,
    const int pixel_height) {
  if (m_renderer == nullptr || document.authored_bytes().empty()) {
    return;
  }
  Debug::I2DCounters counters;
  m_renderer->render_world_text(
      document, presentation_time_ms, m_fonts, pixel_width, pixel_height, counters);
}

void InterfaceManager::set_background_interpolated(const bool interpolated) {
  for (const auto& instance : m_instances) {
    if (instance->background != nullptr) {
      instance->background->set_interpolated(interpolated);
    }
  }
}

void InterfaceManager::set_audio_system(Audio::AudioSystem* audio) {
  m_audio_system = audio;
}

void InterfaceManager::set_window(Window* window) {
  m_window = window;
}

void InterfaceManager::refresh_display_options() {
  if (m_window == nullptr) {
    return;
  }
  for (const auto& instance : m_instances) {
    if (instance->descriptor == nullptr ||
        instance->descriptor->id != static_cast<std::int32_t>(k_options_interface_id)) {
      continue;
    }
    seed_video_settings(*this, *instance);
  }
}

void InterfaceManager::play_ui_sound(
    const InterfaceDescriptor& descriptor, const Audio::UIMenuSoundEvent event) {
  if (m_audio_system == nullptr || !descriptor.sounds.has_value()) {
    return;
  }
  const InterfaceSoundSet& sounds{descriptor.sounds.value()};
  std::string_view path;
  switch (event) {
    case Audio::UIMenuSoundEvent::k_navigate:
      path = sounds.navigate;
      break;
    case Audio::UIMenuSoundEvent::k_confirm:
      path = sounds.confirm;
      break;
    case Audio::UIMenuSoundEvent::k_cancel:
      path = sounds.cancel;
      break;
  }
  static_cast<void>(m_audio_system->play_ui_sound(path, event));
}

void InterfaceManager::apply_game_setting(const std::string_view stable_id) {
  if (stable_id == "enhancements.animation_interpolation") {
    const auto raw{m_game_settings->choice_raw_value(stable_id)};
    set_background_interpolated(raw.value_or(1) != 0);
    return;
  }

  if (stable_id != "enhancements.menu_transition_style") {
    return;
  }
}

bool InterfaceManager::background_interpolated() const {
  for (const auto& instance : m_instances) {
    if (instance->background != nullptr) {
      return instance->background->interpolated();
    }
  }
  return true;
}

bool InterfaceManager::transition_active() const {
  return m_active_transition.has_value();
}

void InterfaceManager::transition_to(InterfaceInstance& instance, I2DState& target) {
  begin_state_transition(instance, target);
}

void InterfaceManager::begin_state_transition(InterfaceInstance& instance, I2DState& target) {
  if (m_active_transition.has_value() || instance.current_state == nullptr ||
      instance.current_state == &target) {
    return;
  }
  const auto raw{m_game_settings->choice_raw_value("enhancements.menu_transition_style")};
  const I2DMenuTransitionStyle style{menu_transition_style_from_raw(raw.value_or(0))};
  const I2DTransitionContext context{instance.descriptor != nullptr && instance.descriptor->id == 29
                                         ? I2DTransitionContext::k_start_menu
                                         : I2DTransitionContext::k_options};
  const float duration{transition_duration(style, context)};
  if (duration <= 0.0F) {
    instance.current_state = &target;
    return;
  }
  m_active_transition =
      ActiveStateTransition{.outgoing = {TransitionLayer{.interface_handle = instance.handle,
                                .state = instance.current_state}},
          .incoming = {TransitionLayer{.interface_handle = instance.handle, .state = &target}},
          .direction = determine_transition_direction(instance.current_state, &target),
          .style = style,
          .context = context,
          .elapsed_seconds = 0.0F,
          .duration_seconds = duration,
          .after_commit = {}};
}

void InterfaceManager::transition_cross_interface(std::vector<TransitionLayer> outgoing,
    std::vector<TransitionLayer> incoming,
    const I2DStateTransitionDirection direction,
    const I2DTransitionContext context,
    std::function<void(InterfaceManager&)> after_commit) {
  if (m_active_transition.has_value() || outgoing.empty() || incoming.empty()) {
    return;
  }
  const auto raw{m_game_settings->choice_raw_value("enhancements.menu_transition_style")};
  const I2DMenuTransitionStyle style{menu_transition_style_from_raw(raw.value_or(0))};
  const float duration{transition_duration(style, context)};
  if (duration <= 0.0F) {
    complete_transition_destination(incoming, std::move(after_commit));
    return;
  }
  m_active_transition = ActiveStateTransition{.outgoing = std::move(outgoing),
      .incoming = std::move(incoming),
      .direction = direction,
      .style = style,
      .context = context,
      .elapsed_seconds = 0.0F,
      .duration_seconds = duration,
      .after_commit = std::move(after_commit)};
}

void InterfaceManager::complete_transition_destination(const std::vector<TransitionLayer>& incoming,
    std::function<void(InterfaceManager&)> after_commit) {
  std::vector<TransitionStateDestination> destinations;
  destinations.reserve(incoming.size());
  for (const TransitionLayer& layer : incoming) {
    InterfaceInstance* instance{find(layer.interface_handle)};
    if (instance != nullptr) {
      destinations.push_back(
          TransitionStateDestination{.instance = instance, .state = layer.state});
    }
  }
  std::function<void()> completion;
  if (after_commit) {
    completion = [this, callback = std::move(after_commit)]() {
      callback(*this);
    };
  }
  commit_transition_destinations(destinations, completion);
}

void InterfaceManager::update_state_transition(const float delta_time) {
  if (!m_active_transition.has_value()) {
    return;
  }
  ActiveStateTransition& transition{m_active_transition.value()};
  transition.elapsed_seconds += std::max(delta_time, 0.0F);
  if (transition.elapsed_seconds < transition.duration_seconds) {
    return;
  }
  const std::vector<TransitionLayer> incoming{transition.incoming};
  const std::function<void(InterfaceManager&)> after_commit{std::move(transition.after_commit)};
  m_active_transition.reset();
  complete_transition_destination(incoming, after_commit);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static) — descriptor API parity
I2DState* InterfaceManager::create_state(InterfaceInstance& instance) {
  auto state{std::make_unique<I2DState>()};
  I2DState* raw{state.get()};
  instance.states.push_back(std::move(state));
  return raw;
}

std::expected<void, std::string> InterfaceManager::load_font(const char key) {
  return m_fonts.load_font(key);
}

void InterfaceManager::capture_next_physical_input(
    std::string token, PhysicalInputCaptureCallback callback) {
  m_pending_physical_input_capture =
      PendingPhysicalInputCapture{.token = std::move(token), .callback = std::move(callback)};
}

void InterfaceManager::cancel_physical_input_capture() {
  m_pending_physical_input_capture.reset();
}

bool InterfaceManager::physical_input_capture_active(const std::string_view token) const {
  return m_pending_physical_input_capture.has_value() &&
         m_pending_physical_input_capture->token == token;
}

void InterfaceManager::select_previous() {
  const InterfaceInstance* instance{focused_instance()};
  if (instance == nullptr || instance->current_state == nullptr ||
      presentation_input_locked(*instance) || transition_active()) {
    return;
  }
  std::vector<I2DTextElement*> selectable{selectable_text_elements(*instance->current_state)};
  if (selectable.empty()) {
    return;
  }
  std::size_t& selected{instance->current_state->selected_element};
  const std::size_t previous{selected};
  selected = previous_selection(selected, selectable.size());
  const I2DTextElement& active{*selectable.at(selected)};
  if (selected != previous && instance->descriptor != nullptr) {
    play_ui_sound(*instance->descriptor, Audio::UIMenuSoundEvent::k_navigate);
  }
  App::Log::debug(LogCategory::I2D, "active element: \"{}\"", text_label(*instance, active));
}

void InterfaceManager::select_next() {
  const InterfaceInstance* instance{focused_instance_mut()};
  if (instance == nullptr || instance->current_state == nullptr ||
      presentation_input_locked(*instance) || transition_active()) {
    return;
  }
  std::vector<I2DTextElement*> selectable{selectable_text_elements(*instance->current_state)};
  if (selectable.empty()) {
    return;
  }
  std::size_t& selected{instance->current_state->selected_element};
  const std::size_t previous{selected};
  selected = next_selection(selected, selectable.size());
  const I2DTextElement& active{*selectable.at(selected)};
  if (selected != previous && instance->descriptor != nullptr) {
    play_ui_sound(*instance->descriptor, Audio::UIMenuSoundEvent::k_navigate);
  }
  App::Log::debug(LogCategory::I2D, "active element: \"{}\"", text_label(*instance, active));
}

void InterfaceManager::adjust_selected(const std::int32_t delta) {
  InterfaceInstance* instance{focused_instance_mut()};
  if (delta == 0 || instance == nullptr || instance->current_state == nullptr ||
      presentation_input_locked(*instance) || transition_active()) {
    return;
  }
  std::vector<I2DTextElement*> selectable{selectable_text_elements(*instance->current_state)};
  if (selectable.empty()) {
    return;
  }
  const I2DTextElement* selected{selectable.at(instance->current_state->selected_element)};
  if (!selected->on_adjust) {
    return;
  }
  // Copy before invocation: an adjustment callback is allowed to rebuild or
  // close its owning interface in later options phases.
  const I2DAdjustCallback adjust{selected->on_adjust};
  const InterfaceDescriptor* descriptor{instance->descriptor};
  const bool changed{adjust(*this, *instance, delta)};
  if (changed && descriptor != nullptr) {
    play_ui_sound(*descriptor, Audio::UIMenuSoundEvent::k_navigate);
  }
}

void InterfaceManager::confirm() {
  InterfaceInstance* instance{focused_instance_mut()};
  if (instance == nullptr || instance->current_state == nullptr || transition_active()) {
    return;
  }
  std::vector<I2DTextElement*> selectable{selectable_text_elements(*instance->current_state)};
  if (selectable.empty()) {
    return;
  }
  const I2DTextElement* selected{selectable.at(instance->current_state->selected_element)};
  const InterfaceDescriptor* descriptor{instance->descriptor};
  if (selected->on_activate) {
    App::Log::debug(LogCategory::I2D, "activate action: \"{}\"", text_label(*instance, *selected));
    if (descriptor != nullptr) {
      play_ui_sound(*descriptor, Audio::UIMenuSoundEvent::k_confirm);
    }
    const I2DActivateCallback on_activate{selected->on_activate};
    on_activate(*this, *instance);
    return;
  }

  I2DState* target{selected->target_state};
  if (target == nullptr) {
    return;  // Adjustable rows may intentionally have no confirm target.
  }
  App::Log::debug(
      LogCategory::I2D, "activate: \"{}\" -> child state", text_label(*instance, *selected));
  if (descriptor != nullptr) {
    play_ui_sound(*descriptor, Audio::UIMenuSoundEvent::k_confirm);
  }
  if (target->on_enter) {
    // A state-specific enter action (e.g. queueing an interface completion)
    // owns the transition; the generic path must not also switch states.
    // Copy first so an action may safely close the interface that owns target.
    const I2DStateEnterCallback on_enter{target->on_enter};
    on_enter(*this, *instance, *target);
    return;
  }
  // Generic child-state transition (no enter action).
  begin_state_transition(*instance, *target);
}

void InterfaceManager::cancel() {
  InterfaceInstance* instance{focused_instance_mut()};
  if (instance == nullptr || instance->current_state == nullptr ||
      presentation_input_locked(*instance) || transition_active()) {
    return;
  }
  if (instance->current_state->on_cancel) {
    // As with on_enter, keep a local copy so the callback may close the
    // interface that owns the state as its final operation.
    const InterfaceDescriptor* descriptor{instance->descriptor};
    if (descriptor != nullptr) {
      play_ui_sound(*descriptor, Audio::UIMenuSoundEvent::k_cancel);
    }
    const I2DStateCancelCallback on_cancel{instance->current_state->on_cancel};
    on_cancel(*this, *instance, *instance->current_state);
    return;
  }
  if (instance->current_state->parent == nullptr) {
    App::Log::debug(LogCategory::I2D, "cancel: already at the root state");
    return;
  }
  const InterfaceDescriptor* descriptor{instance->descriptor};
  if (descriptor != nullptr) {
    play_ui_sound(*descriptor, Audio::UIMenuSoundEvent::k_cancel);
  }
  begin_state_transition(*instance, *instance->current_state->parent);
  App::Log::debug(LogCategory::I2D, "returned to parent state");
}

void InterfaceManager::handle_navigation(const Input::InputManager& input) {
  if (transition_active()) {
    return;
  }
  if (m_pending_physical_input_capture.has_value()) {
    const std::optional<Input::InputSource> pressed{input.last_physical_press()};
    if (!pressed.has_value()) {
      return;
    }
    if (pressed->type == Input::SourceType::k_key &&
        pressed->index == static_cast<std::uint32_t>(SDL_SCANCODE_ESCAPE)) {
      App::Log::debug(LogCategory::Interface, "physical input capture cancelled");
      cancel_physical_input_capture();
      return;
    }
    const PhysicalInputCaptureCallback callback{
        std::move(m_pending_physical_input_capture->callback)};
    cancel_physical_input_capture();
    if (callback) {
      callback(pressed.value());
    }
    return;
  }
  if (input.is_action_pressed(Input::Action::k_menu_up)) {
    select_previous();
  }
  if (input.is_action_pressed(Input::Action::k_menu_down)) {
    select_next();
  }
  if (input.is_action_pressed(Input::Action::k_menu_left)) {
    adjust_selected(-1);
  }
  if (input.is_action_pressed(Input::Action::k_menu_right)) {
    adjust_selected(1);
  }
  if (input.is_action_pressed(Input::Action::k_menu_confirm)) {
    confirm();
  }
  if (input.is_action_pressed(Input::Action::k_menu_cancel)) {
    cancel();
  }
}

namespace {

struct RuntimeKeyMapping {
  SDL_Scancode scancode;
  std::uint32_t dik;

  constexpr RuntimeKeyMapping(const SDL_Scancode scancode_value, const std::uint32_t dik_value)
      : scancode(scancode_value),
        dik(dik_value) {}
};

constexpr std::array<RuntimeKeyMapping, 104> K_RUNTIME_KEY_MAPPINGS{{
    {SDL_SCANCODE_ESCAPE, 0x01U},
    {SDL_SCANCODE_1, 0x02U},
    {SDL_SCANCODE_2, 0x03U},
    {SDL_SCANCODE_3, 0x04U},
    {SDL_SCANCODE_4, 0x05U},
    {SDL_SCANCODE_5, 0x06U},
    {SDL_SCANCODE_6, 0x07U},
    {SDL_SCANCODE_7, 0x08U},
    {SDL_SCANCODE_8, 0x09U},
    {SDL_SCANCODE_9, 0x0AU},
    {SDL_SCANCODE_0, 0x0BU},
    {SDL_SCANCODE_MINUS, 0x0CU},
    {SDL_SCANCODE_EQUALS, 0x0DU},
    {SDL_SCANCODE_BACKSPACE, 0x0EU},
    {SDL_SCANCODE_TAB, 0x0FU},
    {SDL_SCANCODE_Q, 0x10U},
    {SDL_SCANCODE_W, 0x11U},
    {SDL_SCANCODE_E, 0x12U},
    {SDL_SCANCODE_R, 0x13U},
    {SDL_SCANCODE_T, 0x14U},
    {SDL_SCANCODE_Y, 0x15U},
    {SDL_SCANCODE_U, 0x16U},
    {SDL_SCANCODE_I, 0x17U},
    {SDL_SCANCODE_O, 0x18U},
    {SDL_SCANCODE_P, 0x19U},
    {SDL_SCANCODE_LEFTBRACKET, 0x1AU},
    {SDL_SCANCODE_RIGHTBRACKET, 0x1BU},
    {SDL_SCANCODE_RETURN, 0x1CU},
    {SDL_SCANCODE_LCTRL, 0x1DU},
    {SDL_SCANCODE_A, 0x1EU},
    {SDL_SCANCODE_S, 0x1FU},
    {SDL_SCANCODE_D, 0x20U},
    {SDL_SCANCODE_F, 0x21U},
    {SDL_SCANCODE_G, 0x22U},
    {SDL_SCANCODE_H, 0x23U},
    {SDL_SCANCODE_J, 0x24U},
    {SDL_SCANCODE_K, 0x25U},
    {SDL_SCANCODE_L, 0x26U},
    {SDL_SCANCODE_SEMICOLON, 0x27U},
    {SDL_SCANCODE_APOSTROPHE, 0x28U},
    {SDL_SCANCODE_GRAVE, 0x29U},
    {SDL_SCANCODE_LSHIFT, 0x2AU},
    {SDL_SCANCODE_BACKSLASH, 0x2BU},
    {SDL_SCANCODE_Z, 0x2CU},
    {SDL_SCANCODE_X, 0x2DU},
    {SDL_SCANCODE_C, 0x2EU},
    {SDL_SCANCODE_V, 0x2FU},
    {SDL_SCANCODE_B, 0x30U},
    {SDL_SCANCODE_N, 0x31U},
    {SDL_SCANCODE_M, 0x32U},
    {SDL_SCANCODE_COMMA, 0x33U},
    {SDL_SCANCODE_PERIOD, 0x34U},
    {SDL_SCANCODE_SLASH, 0x35U},
    {SDL_SCANCODE_RSHIFT, 0x36U},
    {SDL_SCANCODE_KP_MULTIPLY, 0x37U},
    {SDL_SCANCODE_LALT, 0x38U},
    {SDL_SCANCODE_SPACE, 0x39U},
    {SDL_SCANCODE_CAPSLOCK, 0x3AU},
    {SDL_SCANCODE_F1, 0x3BU},
    {SDL_SCANCODE_F2, 0x3CU},
    {SDL_SCANCODE_F3, 0x3DU},
    {SDL_SCANCODE_F4, 0x3EU},
    {SDL_SCANCODE_F5, 0x3FU},
    {SDL_SCANCODE_F6, 0x40U},
    {SDL_SCANCODE_F7, 0x41U},
    {SDL_SCANCODE_F8, 0x42U},
    {SDL_SCANCODE_F9, 0x43U},
    {SDL_SCANCODE_F10, 0x44U},
    {SDL_SCANCODE_NUMLOCKCLEAR, 0x45U},
    {SDL_SCANCODE_SCROLLLOCK, 0x46U},
    {SDL_SCANCODE_KP_7, 0x47U},
    {SDL_SCANCODE_KP_8, 0x48U},
    {SDL_SCANCODE_KP_9, 0x49U},
    {SDL_SCANCODE_KP_MINUS, 0x4AU},
    {SDL_SCANCODE_KP_4, 0x4BU},
    {SDL_SCANCODE_KP_5, 0x4CU},
    {SDL_SCANCODE_KP_6, 0x4DU},
    {SDL_SCANCODE_KP_PLUS, 0x4EU},
    {SDL_SCANCODE_KP_1, 0x4FU},
    {SDL_SCANCODE_KP_2, 0x50U},
    {SDL_SCANCODE_KP_3, 0x51U},
    {SDL_SCANCODE_KP_0, 0x52U},
    {SDL_SCANCODE_KP_PERIOD, 0x53U},
    {SDL_SCANCODE_F11, 0x57U},
    {SDL_SCANCODE_F12, 0x58U},
    {SDL_SCANCODE_KP_ENTER, 0x9CU},
    {SDL_SCANCODE_RCTRL, 0x9DU},
    {SDL_SCANCODE_KP_DIVIDE, 0xB5U},
    {SDL_SCANCODE_PRINTSCREEN, 0xB7U},
    {SDL_SCANCODE_RALT, 0xB8U},
    {SDL_SCANCODE_PAUSE, 0xC5U},
    {SDL_SCANCODE_HOME, 0xC7U},
    {SDL_SCANCODE_UP, 0xC8U},
    {SDL_SCANCODE_PAGEUP, 0xC9U},
    {SDL_SCANCODE_LEFT, 0xCBU},
    {SDL_SCANCODE_RIGHT, 0xCDU},
    {SDL_SCANCODE_END, 0xCFU},
    {SDL_SCANCODE_DOWN, 0xD0U},
    {SDL_SCANCODE_PAGEDOWN, 0xD1U},
    {SDL_SCANCODE_INSERT, 0xD2U},
    {SDL_SCANCODE_DELETE, 0xD3U},
    {SDL_SCANCODE_LGUI, 0xDBU},
    {SDL_SCANCODE_RGUI, 0xDCU},
    {SDL_SCANCODE_APPLICATION, 0xDDU},
}};

std::optional<std::uint32_t> runtime_dik_from_sdl(const std::uint32_t index) {
  const auto scancode{static_cast<SDL_Scancode>(index)};
  for (const RuntimeKeyMapping& mapping : K_RUNTIME_KEY_MAPPINGS) {
    if (mapping.scancode == scancode) {
      return mapping.dik;
    }
  }
  return std::nullopt;
}

std::string runtime_key_label(const std::uint32_t dik) {
  for (const RuntimeKeyMapping& mapping : K_RUNTIME_KEY_MAPPINGS) {
    if (mapping.dik != dik) {
      continue;
    }
    switch (mapping.scancode) {
      case SDL_SCANCODE_RETURN:
        return "Enter";
      case SDL_SCANCODE_UP:
        return "Up Arrow";
      case SDL_SCANCODE_DOWN:
        return "Down Arrow";
      case SDL_SCANCODE_LEFT:
        return "Left Arrow";
      case SDL_SCANCODE_RIGHT:
        return "Right Arrow";
      default:
        break;
    }
    const char* name{SDL_GetScancodeName(mapping.scancode)};
    if (name != nullptr && name[0] != '\0') {
      return std::string{name};
    }
  }
  return fmt::format("Key 0x{:02X}", dik);
}

std::optional<std::uint32_t> runtime_mouse_offset_from_sdl_button(const std::uint32_t button) {
  if (button == 0U || button > 8U) {
    return std::nullopt;
  }
  return 0x0CU + (button - 1U);
}

std::string runtime_mouse_label(const std::uint32_t offset) {
  if (offset >= 0x0CU && offset <= 0x13U) {
    return fmt::format("Button {}", offset - 0x0CU);
  }
  return fmt::format("Mouse 0x{:02X}", offset);
}

std::string keyboard_mouse_binding_label(const App::Settings::RuntimeControlBindings& bindings,
    const std::size_t group,
    const std::size_t slot) {
  const std::uint32_t keyboard{
      bindings.value(App::Settings::RuntimeControlDevice::k_keyboard, group, slot)};
  const std::uint32_t mouse{
      bindings.value(App::Settings::RuntimeControlDevice::k_mouse, group, slot)};

  std::string label;
  if (keyboard != 0U) {
    label = runtime_key_label(keyboard);
  }
  if (mouse != 0U) {
    if (!label.empty()) {
      label += "; ";
    }
    label += runtime_mouse_label(mouse);
  }
  return label;
}

/// Returns from interface 35 to the resident interface that opened it. The
/// host state is restored before closing the child; close() then re-focuses the
/// most recently opened remaining instance.
void close_options_to_host(InterfaceManager& manager, InterfaceInstance& options_instance) {
  const InterfaceHandle options_handle{options_instance.handle};
  const std::optional<InterfaceHandle> host_handle{options_instance.parent_interface};

  if (host_handle.has_value()) {
    if (const InterfaceInstance* host{manager.find(host_handle.value())}; host != nullptr) {
      const InterfaceHandle host_id{host->handle};
      manager.transition_cross_interface(
          {InterfaceManager::TransitionLayer{
               .interface_handle = host_id, .state = host->current_state},
              InterfaceManager::TransitionLayer{
                  .interface_handle = options_handle, .state = options_instance.current_state}},
          {InterfaceManager::TransitionLayer{
              .interface_handle = host_id, .state = host->root_state}},
          I2DStateTransitionDirection::k_back,
          I2DTransitionContext::k_cross_interface,
          [options_handle, host_id](InterfaceManager& manager_ref) {
            manager_ref.close(options_handle);
            manager_ref.set_focused(host_id);
          });
      return;
    }
  }

  // Must be the final operation that touches options_instance: close destroys
  // the state graph containing the callback currently being executed.
  manager.close(options_handle);
  if (host_handle.has_value()) {
    manager.set_focused(host_handle.value());
  }
}

std::vector<App::Settings::SettingChoice> copy_runtime_choices(
    const InterfaceInstance& instance, const OptionsRowDefinition& definition) {
  std::vector<App::Settings::SettingChoice> choices;
  choices.reserve(definition.choices.size());
  for (const OptionsChoiceDefinition& choice : definition.choices) {
    std::string label;
    if (!choice.literal_label.empty()) {
      label = choice.literal_label;
    } else if (choice.runtime_string_index >= 0) {
      label =
          std::string{instance.strings.at(static_cast<std::size_t>(choice.runtime_string_index))};
    }
    choices.push_back(
        App::Settings::SettingChoice{.label = std::move(label), .raw_value = choice.raw_value});
  }
  return choices;
}

void seed_choice_settings(InterfaceManager& manager,
    const InterfaceInstance& instance,
    const OptionsPageDefinition& page) {
  for (const OptionsRowDefinition& row : page.rows) {
    if (row.kind == OptionsRowKind::k_enum) {
      manager.game_settings().ensure_choice(
          std::string{row.stable_id}, copy_runtime_choices(instance, row), row.default_choice);
    }
  }
}

void seed_video_settings(InterfaceManager& manager, const InterfaceInstance& instance) {
  seed_choice_settings(manager, instance, k_options_video_page);
  if (manager.window() == nullptr) {
    return;
  }
  const DisplayModeCatalog catalog{manager.window()->display_mode_catalog()};
  const DisplayMode mode{manager.window()->actual_display_mode()};
  const std::vector<DisplayResolution>& available_resolutions{
      mode == DisplayMode::k_exclusive_fullscreen ? catalog.exclusive_resolutions
                                                  : catalog.resolutions};
  std::vector<App::Settings::SettingChoice> resolutions;
  resolutions.reserve(available_resolutions.size() + 1U);
  for (const DisplayResolution resolution : available_resolutions) {
    resolutions.push_back(
        App::Settings::SettingChoice{.label = display_resolution_label(resolution),
            .raw_value = pack_display_resolution(resolution).value_or(0)});
  }
  if (const auto stored{unpack_display_resolution(
          manager.game_settings().choice_raw_value("display.resolution").value_or(0))};
      mode != DisplayMode::k_exclusive_fullscreen && stored.has_value() &&
      !std::ranges::any_of(resolutions,
          [raw = pack_display_resolution(stored.value()).value_or(0)](
              const App::Settings::SettingChoice& choice) {
            return choice.raw_value == raw;
          })) {
    resolutions.push_back(
        App::Settings::SettingChoice{.label = display_resolution_label(stored.value()),
            .raw_value = pack_display_resolution(stored.value()).value_or(0)});
  }
  std::ranges::sort(resolutions, {}, [](const App::Settings::SettingChoice& choice) {
    return choice.raw_value;
  });
  manager.game_settings().replace_choice("display.resolution", std::move(resolutions), 0U);
}

void seed_audio_settings(InterfaceManager& manager, const InterfaceInstance& instance) {
  seed_choice_settings(manager, instance, k_options_audio_page);

  // Runtime's three recovered volume controls and OpenNomad's music control
  // use UI values 0..100 in steps of 10.
  manager.game_settings().ensure_number("audio.dialogue_volume", 0, 100, 10, 100);
  manager.game_settings().ensure_number("audio.music_volume", 0, 100, 10, 100);
  manager.game_settings().ensure_number("audio.ambient_volume", 0, 100, 10, 100);
  manager.game_settings().ensure_number("audio.sfx_volume", 0, 100, 10, 100);
}

void seed_game_settings(InterfaceManager& manager, const InterfaceInstance& instance) {
  seed_choice_settings(manager, instance, k_options_game_page);
}

I2DTextElement make_options_text(InterfaceManager& manager,
    const OptionsRowDefinition& definition,
    const std::size_t row_index,
    const std::size_t active_count,
    I2DState* target_state) {
  I2DTextElement text;
  if (!definition.literal_label.empty()) {
    text.literal_text = std::string{definition.literal_label};
  } else if (definition.runtime_label_string_index >= 0) {
    text.string_index = static_cast<std::uint16_t>(definition.runtime_label_string_index);
  }
  const bool choice_row{
      definition.kind == OptionsRowKind::k_enum || definition.kind == OptionsRowKind::k_dynamic};
  const bool slider_row{definition.kind == OptionsRowKind::k_slider};
  const bool value_row{choice_row || slider_row};
  text.font_key = value_row ? k_options_value_font_key : k_options_root_font_key;
  text.bounds = I2DRect{.x = k_options_row_x,
      .y = runtime_options_row_y(row_index, active_count, OptionsInvocationMode::k_start_menu),
      .width = k_options_row_width,
      .height = k_options_row_height};
  text.runtime_flags = value_row ? k_options_value_text_flags : k_options_text_flags;
  text.target_state = target_state;
  if (definition.accent) {
    // Runtime uses its warm active/accent colour for OPTIONS page headings.
    text.red = 255;
    text.green = 101;
    text.blue = 66;
  }
  if (slider_row) {
    text.layout = I2DTextLayout::k_option_slider;
    const std::string setting_id{definition.stable_id};
    const App::Settings::GameSettings* settings{&manager.game_settings()};
    text.value_scalar = [settings, setting_id]() {
      return settings->number_fraction(setting_id);
    };
    text.on_adjust =
        [setting_id](InterfaceManager& manager_ref, InterfaceInstance&, const std::int32_t delta) {
          const bool changed{manager_ref.game_settings().adjust_number(setting_id, delta)};
          if (!changed) {
            return false;
          }
          App::Log::debug(LogCategory::Interface,
              "setting {} -> {}",
              setting_id,
              manager_ref.game_settings().number_value(setting_id).value_or(0));
          return true;
        };
  }
  if (choice_row) {
    text.layout = I2DTextLayout::k_option_pair;
    const std::string setting_id{definition.stable_id};
    const App::Settings::GameSettings* settings{&manager.game_settings()};
    const Window* const window{manager.window()};
    text.value_text = [settings, setting_id]() {
      return settings->choice_label(setting_id);
    };
    if (setting_id == "display.resolution" && window != nullptr) {
      text.value_text = [settings, setting_id, window]() {
        if (window->actual_display_mode() == DisplayMode::k_borderless_fullscreen) {
          return fmt::format(
              "Desktop ({})", display_resolution_label(window->display_mode_catalog().desktop));
        }
        return settings->choice_label(setting_id);
      };
    }
    text.on_adjust =
        [setting_id](InterfaceManager& manager_ref, InterfaceInstance&, const std::int32_t delta) {
          if (setting_id == "display.resolution" && manager_ref.window() != nullptr &&
              manager_ref.window()->actual_display_mode() == DisplayMode::k_borderless_fullscreen) {
            return false;
          }
          const bool changed{manager_ref.game_settings().adjust_choice(setting_id, delta)};
          if (!changed) {
            return false;
          }
          const auto raw{manager_ref.game_settings().choice_raw_value(setting_id)};
          App::Log::debug(LogCategory::Interface,
              "setting {} -> \"{}\" raw={}",
              setting_id,
              manager_ref.game_settings().choice_label(setting_id),
              raw.has_value() ? raw.value() : 0);
          return true;
        };
  }
  return text;
}

void populate_options_page(InterfaceManager& manager,
    I2DState& state,
    const OptionsPageDefinition& page,
    I2DState& back_target) {
  I2DGroup rows;
  rows.runtime_flags = k_options_text_flags;
  std::size_t row_index{0};
  for (const OptionsRowDefinition& row : page.rows) {
    I2DState* target_state{row.kind == OptionsRowKind::k_back ? &back_target : nullptr};
    rows.elements.push_back(I2DElement{
        .data = make_options_text(manager, row, row_index, page.rows.size(), target_state),
        .presentation = I2DPresentationHints{}});
    ++row_index;
  }
  state.groups.push_back(std::move(rows));
  state.selected_element = 0U;
}

I2DTextElement make_keyboard_binding_text(InterfaceManager& manager,
    const OptionsBindingRowDefinition& definition,
    const std::size_t row_index,
    const std::size_t active_count) {
  I2DTextElement text;
  text.string_index = static_cast<std::uint16_t>(definition.runtime_label_string_index);
  text.font_key = k_options_value_font_key;
  text.bounds = I2DRect{.x = k_options_row_x,
      .y = runtime_options_row_y(row_index, active_count, OptionsInvocationMode::k_start_menu),
      .width = k_options_row_width,
      .height = k_options_row_height};
  text.runtime_flags = k_options_value_text_flags;
  text.layout = I2DTextLayout::k_option_pair;

  const std::size_t group{definition.group};
  const std::size_t slot{definition.slot};
  const std::string capture_token{std::string{definition.stable_id}};
  const InterfaceManager* manager_ptr{&manager};
  const App::Settings::RuntimeControlBindings& bindings{
      manager.game_settings().runtime_control_bindings()};

  text.value_text = [manager_ptr, &bindings, capture_token, group, slot]() {
    if (manager_ptr->physical_input_capture_active(capture_token)) {
      return std::string{"Press a key or mouse button..."};
    }
    return keyboard_mouse_binding_label(bindings, group, slot);
  };

  text.on_activate = [capture_token, group, slot](
                         InterfaceManager& manager_ref, InterfaceInstance&) {
    App::Log::debug(LogCategory::Interface, "capture binding group={} slot={}", group, slot);
    manager_ref.capture_next_physical_input(
        capture_token, [&manager_ref, group, slot](const Input::InputSource& source) {
          if (source.type == Input::SourceType::k_key) {
            const std::optional<std::uint32_t> dik{runtime_dik_from_sdl(source.index)};
            if (!dik.has_value()) {
              App::Log::warn(LogCategory::Interface,
                  "key scancode {} has no Runtime DIK mapping",
                  source.index);
              return;
            }
            manager_ref.game_settings().set_runtime_control_binding(
                App::Settings::RuntimeControlDevice::k_keyboard, group, slot, dik.value());
            return;
          }

          if (source.type == Input::SourceType::k_mouse_button) {
            const std::optional<std::uint32_t> offset{
                runtime_mouse_offset_from_sdl_button(source.index)};
            if (!offset.has_value()) {
              return;
            }
            manager_ref.game_settings().set_runtime_control_binding(
                App::Settings::RuntimeControlDevice::k_mouse, group, slot, offset.value());
          }
        });
  };
  return text;
}

void populate_keyboard_binding_page(InterfaceManager& manager,
    I2DState& state,
    const OptionsBindingPageDefinition& page,
    I2DState& back_target) {
  const std::size_t active_count{page.bindings.size() + 3U};
  I2DGroup rows;
  rows.runtime_flags = k_options_text_flags;

  const OptionsRowDefinition title{.stable_id = page.stable_id,
      .runtime_option_index = page.runtime_title_option_index,
      .runtime_label_string_index = page.runtime_title_string_index,
      .kind = OptionsRowKind::k_submenu,
      .choices = {},
      .default_choice = 0,
      .literal_label = {},
      .accent = true};
  rows.elements.push_back(
      I2DElement{.data = make_options_text(manager, title, 0U, active_count, nullptr),
          .presentation = I2DPresentationHints{}});

  std::size_t row_index{1U};
  for (const OptionsBindingRowDefinition& binding : page.bindings) {
    rows.elements.push_back(
        I2DElement{.data = make_keyboard_binding_text(manager, binding, row_index, active_count),
            .presentation = I2DPresentationHints{}});
    ++row_index;
  }

  I2DTextElement restore;
  restore.string_index = static_cast<std::uint16_t>(k_options_restore_defaults_string_index);
  restore.literal_text = "Reset to Defaults";
  restore.font_key = k_options_value_font_key;
  restore.bounds = I2DRect{.x = k_options_row_x,
      .y = runtime_options_row_y(row_index, active_count, OptionsInvocationMode::k_start_menu),
      .width = k_options_row_width,
      .height = k_options_row_height};
  restore.runtime_flags = k_options_value_text_flags;
  restore.on_activate = [](InterfaceManager& manager_ref, InterfaceInstance&) {
    manager_ref.game_settings().restore_runtime_keyboard_mouse_defaults();
    App::Log::info(LogCategory::Interface, "restored Runtime keyboard/mouse control defaults");
  };
  rows.elements.push_back(
      I2DElement{.data = std::move(restore), .presentation = I2DPresentationHints{}});
  ++row_index;

  const OptionsRowDefinition back{.stable_id = "controls.keyboard.back",
      .runtime_option_index = 72,
      .runtime_label_string_index = 80,
      .kind = OptionsRowKind::k_back,
      .choices = {},
      .default_choice = 0,
      .literal_label = {},
      .accent = false};
  rows.elements.push_back(
      I2DElement{.data = make_options_text(manager, back, row_index, active_count, &back_target),
          .presentation = I2DPresentationHints{}});

  state.groups.push_back(std::move(rows));
  state.selected_element = 0U;
}

// Runtime StartMenu initializer: 0x00479D10.
// Recovered root state: 0x004CF218.
// Recovered text elements:
//   0x004CE6F0 -> IAM index 0
//   0x004CE738 -> IAM index 1
//   0x004CE780 -> IAM index 4
//   0x004CE7C8 -> IAM index 5
void initialize_start_menu(InterfaceManager& manager, InterfaceInstance& instance) {
  App::Log::debug(LogCategory::I2D, "initializing START MENU root state");

  I2DState* root{manager.create_state(instance)};
  I2DState* new_game{manager.create_state(instance)};
  I2DState* load_game{manager.create_state(instance)};
  I2DState* options{manager.create_state(instance)};
  I2DState* quit{manager.create_state(instance)};

  // Synthetic OpenNomad action state. Runtime stores the Yes action directly
  // on the text element (callback 0x0047BC10); our current I2D abstraction
  // represents element actions through target-state enter callbacks.
  I2DState* quit_yes_action{manager.create_state(instance)};

  if (root == nullptr || new_game == nullptr || load_game == nullptr || options == nullptr ||
      quit == nullptr || quit_yes_action == nullptr) {
    App::Log::error(LogCategory::I2D, "failed to allocate the START MENU state graph");
    return;
  }

  new_game->parent = root;
  load_game->parent = root;
  options->parent = root;
  quit->parent = root;
  quit_yes_action->parent = quit;

  // Runtime Quit-state initializer @ 0x0047BBB0:
  //   WORD [0x004CEF12] = 1   -> default choice is "No"
  //   WORD [0x004CF004] = 5   -> title string is IAM/Menu[5] ("Quit")
  //
  // on_enter owns this transition because it must also establish the
  // Runtime-authored initial selection.
  quit->on_enter =
      [](InterfaceManager& manager_ref, InterfaceInstance& instance_ref, I2DState& state_ref) {
        state_ref.selected_element = k_start_menu_quit_default_choice;
        manager_ref.transition_to(instance_ref, state_ref);
      };

  // Runtime Yes callback @ 0x0047BC10 ultimately executes
  // PostQuitMessage(0). SDL_EVENT_QUIT is the direct SDL equivalent: enqueue
  // the request and let Application::process_event() perform normal shutdown
  // rather than tearing the engine down from inside interface iteration.
  quit_yes_action->on_enter = [](InterfaceManager&, InterfaceInstance&, I2DState&) {
    SDL_Event quit_event{};
    quit_event.type = SDL_EVENT_QUIT;
    if (!SDL_PushEvent(&quit_event)) {
      App::Log::error(LogCategory::Interface, "failed to post quit event: {}", SDL_GetError());
    }
  };

  // Runtime New Game callback @ 0x0047A2B0 stores result 3 in the interface
  // state. The generic interface-completion path later writes that result to
  // AREA opcode 0x46's destination global (global 19 for interface 29).
  //
  // AREA 118 tests global 19 against zero; result 3 selects the branch which
  // materializes Kay'l and starts the GRID intro scripts.
  new_game->on_enter =
      [](InterfaceManager& manager_ref, InterfaceInstance& instance_ref, I2DState&) {
        manager_ref.request_completion(instance_ref.handle, 3);
      };

  // Runtime keeps interface 29 resident while interface 35 OPTIONS is active.
  // The parent switches to a presentation-only host state containing the
  // existing CLOUD background and logo; interface 35 then draws its own rows
  // over that resident parent. This matches descriptor 35 having no bitmap.
  options->on_enter = [](InterfaceManager& manager_ref,
                          InterfaceInstance& instance_ref,
                          I2DState& state_ref) {
    const InterfaceHandle host_handle{instance_ref.handle};

    auto opened{manager_ref.open(InterfaceOpenRequest{
        .interface_id = k_options_interface_id, .operand_b = -1, .operand_c = -1})};
    if (!opened) {
      App::Log::error(LogCategory::Interface, "failed to open OPTIONS: {}", opened.error());
      instance_ref.current_state = instance_ref.root_state;
      manager_ref.set_focused(host_handle);
      return;
    }

    if (InterfaceInstance* child{manager_ref.find(opened.value())}; child != nullptr) {
      child->parent_interface = host_handle;
      manager_ref.transition_cross_interface(
          {InterfaceManager::TransitionLayer{
              .interface_handle = host_handle, .state = instance_ref.current_state}},
          {InterfaceManager::TransitionLayer{.interface_handle = host_handle, .state = &state_ref},
              InterfaceManager::TransitionLayer{
                  .interface_handle = child->handle, .state = child->current_state}},
          I2DStateTransitionDirection::k_forward,
          I2DTransitionContext::k_cross_interface,
          [child_handle = child->handle](InterfaceManager& completion_manager) {
            completion_manager.set_focused(child_handle);
          });
    }
  };

  // Animated background: IMAGES/CLOUD.BMP. Missing source degrades to no
  // background (the canvas stays clear) rather than an invented asset.
  if (auto background{I2DBumpBackground::create()}) {
    background.value()->set_interpolated(
        manager.game_settings()
            .choice_raw_value("enhancements.animation_interpolation")
            .value_or(1) != 0);
    root->background = background->get();
    options->background = background->get();
    quit->background = background->get();
    instance.background = std::move(background).value();
  } else {
    App::Log::warn(LogCategory::I2D, "background unavailable: {}", background.error());
  }

  // Font key 'I' -> MENUINTR (the FNT renderer falls back to the TTF).
  if (auto result{manager.load_font('I')}; !result) {
    App::Log::warn(LogCategory::I2D, "font 'I' unavailable: {}", result.error());
  }

  // Runtime's Yes/No confirmation elements use font key 'S' -> SNEAK.FNT.
  if (auto result{manager.load_font('S')}; !result) {
    App::Log::warn(LogCategory::I2D, "font 'S' unavailable: {}", result.error());
  }

  // Runtime bitmap element approximately 0x004CF1A8.
  // Source/destination: 0,0,640,150. Raw flags: 0x40000100. Blit mode 0x03:
  // bit 0 = DDBLT_KEYSRC (source key), bit 1 = DDBLT_KEYDEST (destination
  // key, value not yet recovered).
  //
  // The recovered rectangle is preserved verbatim; the top-centre anchor and
  // small top margin are OpenNomad modernization adjustments supplied as
  // presentation hints, not mutations of the Runtime-authored coordinates.
  I2DGroup bitmap_group;
  bitmap_group.elements.push_back(
      I2DElement{.data = I2DBitmapElement{.source = k_start_menu_bitmap_rect,
                     .destination = k_start_menu_bitmap_rect,
                     .runtime_flags = k_start_menu_bitmap_flags,
                     .runtime_blit_mode = k_start_menu_bitmap_blit_mode},
          .presentation = I2DPresentationHints{.scale_policy = I2DScalePolicy::k_reference_canvas,
              .anchor_top_center = true,
              .top_margin_reference = k_start_menu_logo_top_margin,
              .top_center_scale = k_start_menu_logo_scale,
              .clamp_width_to_viewport = true}});
  // OPTIONS is a separate resident interface with no descriptor bitmap. Keep
  // the START MENU logo in its host state so interface 35 can overlay only the
  // option rows, just as Runtime's residency model implies.
  options->groups.push_back(bitmap_group);
  root->groups.push_back(std::move(bitmap_group));

  // Runtime text group raw flags: 0x80000010. The 640 px wide rectangles and
  // observed Runtime behaviour indicate centred labels; the exact symbolic
  // meaning of every flag bit is not yet established.
  //
  // Recovered text elements (address -> IAM index):
  //   0x004CE6F0 -> 0, 0x004CE738 -> 1, 0x004CE780 -> 4, 0x004CE7C8 -> 5.
  const std::array<I2DState*, 4> child_states{new_game, load_game, options, quit};
  I2DGroup text_group;
  text_group.runtime_flags = k_start_menu_text_group_flags;
  for (std::size_t index{0}; index < k_start_menu_root_entries.size(); ++index) {
    const RecoveredTextEntry& entry{k_start_menu_root_entries.at(index)};
    text_group.elements.push_back(
        I2DElement{.data = I2DTextElement{.string_index = entry.string_index,
                       .font_key = entry.font_key,
                       .bounds = I2DRect{.x = entry.x,
                           .y = k_start_menu_modern_y.at(index),
                           .width = entry.width,
                           .height = k_start_menu_modern_text_height},
                       .red = 255,
                       .green = 255,
                       .blue = 255,
                       .target_state = child_states.at(index),
                       .on_activate = {},
                       .on_adjust = {},
                       .literal_text = {},
                       .layout = I2DTextLayout::k_centered,
                       .value_text = {},
                       .value_scalar = {}},
            .presentation = I2DPresentationHints{}});
  }
  root->groups.push_back(std::move(text_group));

  // Quit confirmation title. Runtime changes this element's string index to
  // IAM/Menu[5] ("Quit") immediately before entering the state.
  I2DGroup quit_title_group;
  quit_title_group.runtime_flags = k_start_menu_text_group_flags;
  quit_title_group.elements.push_back(
      I2DElement{.data = I2DTextElement{.string_index = k_start_menu_quit_title.string_index,
                     .font_key = k_start_menu_quit_title.font_key,
                     .bounds = I2DRect{.x = k_start_menu_quit_title.x,
                         .y = k_start_menu_quit_title.y,
                         .width = k_start_menu_quit_title.width,
                         .height = k_start_menu_quit_title.height},
                     .red = 255,
                     .green = 255,
                     .blue = 255,
                     .target_state = nullptr,
                     .on_activate = {},
                     .on_adjust = {},
                     .literal_text = {},
                     .layout = I2DTextLayout::k_centered,
                     .value_text = {},
                     .value_scalar = {}},
          .presentation = I2DPresentationHints{}});
  quit->groups.push_back(std::move(quit_title_group));

  // Runtime confirmation selector:
  //   IAM/Menu[6] = "Yes" -> callback 0x0047BC10 -> PostQuitMessage(0)
  //   IAM/Menu[7] = "No"  -> parent/root state 0x004CF218
  //
  // The static Yes/No templates both start at y=330, but Runtime's
  // StartMenu_Initialize calls group-layout helper 0x00429680 with start
  // y=260 and step=60, yielding the effective bounds stored above. Both
  // choices remain visible; selection only controls their active/inactive tint.
  const std::array<I2DState*, 2> quit_targets{quit_yes_action, root};
  I2DGroup quit_choice_group;
  quit_choice_group.runtime_flags = k_start_menu_text_group_flags;
  for (std::size_t index{0}; index < k_start_menu_quit_choices.size(); ++index) {
    const RecoveredTextEntry& entry{k_start_menu_quit_choices.at(index)};
    quit_choice_group.elements.push_back(I2DElement{
        .data = I2DTextElement{.string_index = entry.string_index,
            .font_key = entry.font_key,
            .bounds =
                I2DRect{.x = entry.x, .y = entry.y, .width = entry.width, .height = entry.height},
            .red = 255,
            .green = 255,
            .blue = 255,
            .target_state = quit_targets.at(index),
            .on_activate = {},
            .on_adjust = {},
            .literal_text = {},
            .layout = I2DTextLayout::k_centered,
            .value_text = {},
            .value_scalar = {}},
        .presentation = I2DPresentationHints{}});
  }
  quit->groups.push_back(std::move(quit_choice_group));
  quit->selected_element = k_start_menu_quit_default_choice;

  instance.root_state = root;
  instance.current_state = root;
  root->selected_element = 0U;

  App::Log::debug(LogCategory::I2D, "root state: 4 selectable text elements");
  App::Log::debug(LogCategory::I2D, "active element: string[{}] \"{}\"", 0, instance.strings.at(0));
}

// Runtime StartMenu destroy callback: 0x00479F30. Resource release is RAII;
// this callback exists to mirror the descriptor contract and to log teardown.
void destroy_start_menu(
    [[maybe_unused]] InterfaceManager& manager, [[maybe_unused]] InterfaceInstance& instance) {
  App::Log::debug(LogCategory::I2D, "destroying START MENU state");
}

// Runtime OPTIONS initializer: 0x00490D50.
// Recovered root state: 0x004DD438.
// Root page builder: 0x00491200.
//
// Video, Audio and Game are declarative Runtime-derived pages. Controls stays
// deferred because capture/rebinding needs its own input-system phase.
void initialize_options(InterfaceManager& manager, InterfaceInstance& instance) {
  App::Log::debug(LogCategory::I2D, "initializing OPTIONS root state");

  I2DState* root{manager.create_state(instance)};
  I2DState* video{manager.create_state(instance)};
  I2DState* audio{manager.create_state(instance)};
  I2DState* game{manager.create_state(instance)};
  I2DState* controls{manager.create_state(instance)};
  I2DState* enhancements{manager.create_state(instance)};
  I2DState* keyboard_categories{manager.create_state(instance)};
  I2DState* gamepad_action{manager.create_state(instance)};
  I2DState* mouse_settings_action{manager.create_state(instance)};
  I2DState* keyboard_group0{manager.create_state(instance)};
  I2DState* keyboard_group1{manager.create_state(instance)};
  I2DState* keyboard_group2{manager.create_state(instance)};
  I2DState* keyboard_group3{manager.create_state(instance)};
  I2DState* back_action{manager.create_state(instance)};

  if (root == nullptr || video == nullptr || audio == nullptr || game == nullptr ||
      controls == nullptr || enhancements == nullptr || keyboard_categories == nullptr ||
      gamepad_action == nullptr || mouse_settings_action == nullptr || keyboard_group0 == nullptr ||
      keyboard_group1 == nullptr || keyboard_group2 == nullptr || keyboard_group3 == nullptr ||
      back_action == nullptr) {
    App::Log::error(LogCategory::I2D, "failed to allocate the OPTIONS state graph");
    return;
  }

  const std::array<I2DState*, 6> targets{video, audio, game, controls, enhancements, back_action};
  for (I2DState* target : targets) {
    target->parent = root;
  }

  keyboard_categories->parent = controls;
  enhancements->parent = root;
  gamepad_action->parent = controls;
  mouse_settings_action->parent = controls;
  keyboard_group0->parent = keyboard_categories;
  keyboard_group1->parent = keyboard_categories;
  keyboard_group2->parent = keyboard_categories;
  keyboard_group3->parent = keyboard_categories;

  gamepad_action->on_enter = [](InterfaceManager&, InterfaceInstance&, I2DState&) {
    App::Log::info(LogCategory::Interface,
        "Controller settings are deferred to the modern SDL gamepad controls phase");
  };
  mouse_settings_action->on_enter = [](InterfaceManager&, InterfaceInstance&, I2DState&) {
    App::Log::info(LogCategory::Interface,
        "Mouse settings are deferred until Runtime's type-1 sensitivity "
        "lookup semantics are fully recovered");
  };

  back_action->on_enter =
      [](InterfaceManager& manager_ref, InterfaceInstance& instance_ref, I2DState&) {
        close_options_to_host(manager_ref, instance_ref);
      };
  root->on_cancel = [](InterfaceManager& manager_ref, InterfaceInstance& instance_ref, I2DState&) {
    close_options_to_host(manager_ref, instance_ref);
  };

  // Runtime type 2/6 rows use SNEAK.FNT; enum/dynamic Video rows use
  // JOURNAL.FNT.
  if (auto result{manager.load_font(k_options_root_font_key)}; !result) {
    App::Log::warn(LogCategory::I2D,
        "font '{}' unavailable for OPTIONS: {}",
        k_options_root_font_key,
        result.error());
  }
  if (auto result{manager.load_font(k_options_value_font_key)}; !result) {
    App::Log::warn(LogCategory::I2D,
        "font '{}' unavailable for OPTIONS values: {}",
        k_options_value_font_key,
        result.error());
  }

  seed_video_settings(manager, instance);
  seed_audio_settings(manager, instance);
  seed_game_settings(manager, instance);
  I2DGroup root_rows;
  root_rows.runtime_flags = k_options_text_flags;
  std::size_t index{0};
  for (const OptionsRowDefinition& row : k_options_root_page.rows) {
    root_rows.elements.push_back(
        I2DElement{.data = make_options_text(
                       manager, row, index, k_options_root_page.rows.size(), targets.at(index)),
            .presentation = I2DPresentationHints{}});
    ++index;
  }
  root->groups.push_back(std::move(root_rows));
  root->selected_element = 0U;

  // Page population is intentionally generic: adding a future Runtime or
  // OpenNomad page is data + settings registration, not another renderer path.
  populate_options_page(manager, *video, k_options_video_page, *root);
  populate_options_page(manager, *audio, k_options_audio_page, *root);
  populate_options_page(manager, *game, k_options_game_page, *root);
  populate_options_page(manager, *enhancements, k_options_enhancements_page, *root);

  // Controls root. Runtime's title is static; the four following entries are
  // Keyboard & Mouse, Controller, Mouse and Back.
  const std::array<I2DState*, 5> controls_targets{
      nullptr, keyboard_categories, gamepad_action, mouse_settings_action, root};
  I2DGroup controls_rows;
  controls_rows.runtime_flags = k_options_text_flags;
  std::size_t controls_index{0};
  for (const OptionsRowDefinition& row : k_options_controls_page.rows) {
    controls_rows.elements.push_back(I2DElement{.data = make_options_text(manager,
                                                    row,
                                                    controls_index,
                                                    k_options_controls_page.rows.size(),
                                                    controls_targets.at(controls_index)),
        .presentation = I2DPresentationHints{}});
    ++controls_index;
  }
  controls->groups.push_back(std::move(controls_rows));
  controls->selected_element = 0U;

  // Keyboard/mouse category menu, Runtime builder 0x00491BB0 in device mode 1.
  const std::array<I2DState*, 6> category_targets{
      nullptr, keyboard_group0, keyboard_group1, keyboard_group2, keyboard_group3, controls};
  I2DGroup category_rows;
  category_rows.runtime_flags = k_options_text_flags;
  std::size_t category_index{0};
  for (const OptionsRowDefinition& row : k_options_keyboard_categories_page.rows) {
    category_rows.elements.push_back(I2DElement{.data = make_options_text(manager,
                                                    row,
                                                    category_index,
                                                    k_options_keyboard_categories_page.rows.size(),
                                                    category_targets.at(category_index)),
        .presentation = I2DPresentationHints{}});
    ++category_index;
  }
  keyboard_categories->groups.push_back(std::move(category_rows));
  keyboard_categories->selected_element = 0U;

  populate_keyboard_binding_page(
      manager, *keyboard_group0, k_options_keyboard_group0_page, *keyboard_categories);
  populate_keyboard_binding_page(
      manager, *keyboard_group1, k_options_keyboard_group1_page, *keyboard_categories);
  populate_keyboard_binding_page(
      manager, *keyboard_group2, k_options_keyboard_group2_page, *keyboard_categories);
  populate_keyboard_binding_page(
      manager, *keyboard_group3, k_options_keyboard_group3_page, *keyboard_categories);

  instance.root_state = root;
  instance.current_state = root;

  App::Log::debug(LogCategory::I2D,
      "OPTIONS root: {} selectable rows; active=\"{}\"",
      k_options_root_page.rows.size(),
      instance.strings.at(
          static_cast<std::uint16_t>(k_options_root_page.rows.front().runtime_label_string_index)));
}

// Runtime OPTIONS destroy callback: 0x00490F30. Resource release remains RAII.
void destroy_options(InterfaceManager& manager, [[maybe_unused]] InterfaceInstance& instance) {
  manager.cancel_physical_input_capture();
  App::Log::debug(LogCategory::I2D, "destroying OPTIONS state");
}

}  // namespace

}  // namespace App::Interface
