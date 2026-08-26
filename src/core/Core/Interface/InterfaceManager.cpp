#include "Core/Interface/InterfaceManager.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/Input/InputAction.hpp"
#include "Core/Input/InputManager.hpp"
#include "Core/Interface/I2DBumpBackground.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Interface/I2DRenderer.hpp"
#include "Core/Interface/InterfaceDescriptor.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Interface/InterfacePresentation.hpp"
#include "Core/Interface/OptionsMenuLayout.hpp"
#include "Core/Interface/StartMenuLayout.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/BmpImage.hpp"
#include "Core/Omikron/IamStringTable.hpp"
#include "Core/Texture.hpp"

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
      InterfaceDescriptor{.id = 29,
          .name = "OMK START MENU",
          .bitmap_name = "gfxint.bmp",
          .string_table_name = "Menu",
          .companion_interface = 35,
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

InterfaceManager::InterfaceManager() = default;

InterfaceManager::~InterfaceManager() {
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
}

void InterfaceManager::render(const int pixel_width, const int pixel_height) {
  APP_PROFILE_FUNCTION();

  if (m_renderer == nullptr) {
    return;
  }
  Debug::I2DCounters counters;
  for (const auto& instance : m_instances) {
    m_renderer->render(*instance, m_fonts, pixel_width, pixel_height, counters);
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

void InterfaceManager::render_world_subtitle(
    const std::string_view text, const int pixel_width, const int pixel_height) {
  if (m_renderer == nullptr || text.empty()) {
    return;
  }
  Debug::I2DCounters counters;
  m_renderer->render_world_subtitle(text, m_fonts, pixel_width, pixel_height, counters);
}

void InterfaceManager::set_background_interpolated(const bool interpolated) {
  for (const auto& instance : m_instances) {
    if (instance->background != nullptr) {
      instance->background->set_interpolated(interpolated);
    }
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

void InterfaceManager::select_previous() {
  const InterfaceInstance* instance{focused_instance_mut()};
  if (instance == nullptr || instance->current_state == nullptr ||
      presentation_input_locked(*instance)) {
    return;
  }
  std::vector<I2DTextElement*> selectable{selectable_text_elements(*instance->current_state)};
  if (selectable.empty()) {
    return;
  }
  std::size_t& selected{instance->current_state->selected_element};
  if (selected == 0U) {
    selected = selectable.size() - 1U;
  } else {
    --selected;
  }
  const I2DTextElement& active{*selectable.at(selected)};
  App::Log::debug(LogCategory::I2D, "active element: \"{}\"", text_label(*instance, active));
}

void InterfaceManager::select_next() {
  const InterfaceInstance* instance{focused_instance_mut()};
  if (instance == nullptr || instance->current_state == nullptr ||
      presentation_input_locked(*instance)) {
    return;
  }
  std::vector<I2DTextElement*> selectable{selectable_text_elements(*instance->current_state)};
  if (selectable.empty()) {
    return;
  }
  std::size_t& selected{instance->current_state->selected_element};
  selected = (selected + 1U) % selectable.size();
  const I2DTextElement& active{*selectable.at(selected)};
  App::Log::debug(LogCategory::I2D, "active element: \"{}\"", text_label(*instance, active));
}

void InterfaceManager::adjust_selected(const std::int32_t delta) {
  InterfaceInstance* instance{focused_instance_mut()};
  if (delta == 0 || instance == nullptr || instance->current_state == nullptr ||
      presentation_input_locked(*instance)) {
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
  adjust(*this, *instance, delta);
}

void InterfaceManager::confirm() {
  InterfaceInstance* instance{focused_instance_mut()};
  if (instance == nullptr || instance->current_state == nullptr) {
    return;
  }
  std::vector<I2DTextElement*> selectable{selectable_text_elements(*instance->current_state)};
  if (selectable.empty()) {
    return;
  }
  const I2DTextElement* selected{selectable.at(instance->current_state->selected_element)};
  I2DState* target{selected->target_state};
  if (target == nullptr) {
    return;  // Adjustable rows may intentionally have no confirm target.
  }
  App::Log::debug(
      LogCategory::I2D, "activate: \"{}\" -> child state", text_label(*instance, *selected));
  if (target->on_enter) {
    // A state-specific enter action (e.g. queueing an interface completion)
    // owns the transition; the generic path must not also switch states.
    // Copy first so an action may safely close the interface that owns target.
    const I2DStateEnterCallback on_enter{target->on_enter};
    on_enter(*this, *instance, *target);
    return;
  }
  // Generic child-state transition (no enter action).
  instance->current_state = target;
}

void InterfaceManager::cancel() {
  InterfaceInstance* instance{focused_instance_mut()};
  if (instance == nullptr || instance->current_state == nullptr ||
      presentation_input_locked(*instance)) {
    return;
  }
  if (instance->current_state->on_cancel) {
    // As with on_enter, keep a local copy so the callback may close the
    // interface that owns the state as its final operation.
    const I2DStateCancelCallback on_cancel{instance->current_state->on_cancel};
    on_cancel(*this, *instance, *instance->current_state);
    return;
  }
  if (instance->current_state->parent == nullptr) {
    App::Log::debug(LogCategory::I2D, "cancel: already at the root state");
    return;
  }
  instance->current_state = instance->current_state->parent;
  App::Log::debug(LogCategory::I2D, "returned to parent state");
}

void InterfaceManager::handle_navigation(const Input::InputManager& input) {
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

/// Returns from interface 35 to the resident interface that opened it. The
/// host state is restored before closing the child; close() then re-focuses the
/// most recently opened remaining instance.
void close_options_to_host(InterfaceManager& manager, InterfaceInstance& options_instance) {
  const InterfaceHandle options_handle{options_instance.handle};
  const std::optional<InterfaceHandle> host_handle{options_instance.parent_interface};

  if (host_handle.has_value()) {
    if (InterfaceInstance* host{manager.find(host_handle.value())}; host != nullptr) {
      host->current_state = host->root_state;
    }
  }

  // Must be the final operation that touches options_instance: close destroys
  // the state graph containing the callback currently being executed.
  manager.close(options_handle);
  if (host_handle.has_value()) {
    manager.set_focused(host_handle.value());
  }
}

I2DTextElement make_options_text(const OptionsRowDefinition& definition,
    const std::size_t row_index,
    const std::size_t active_count,
    I2DState* target_state) {
  I2DTextElement text;
  if (definition.runtime_string_index >= 0) {
    text.string_index = static_cast<std::uint16_t>(definition.runtime_string_index);
  } else {
    text.literal_text = std::string{definition.literal_label};
  }
  text.font_key = k_options_root_font_key;
  text.bounds = I2DRect{.x = k_options_row_x,
      .y = runtime_options_row_y(row_index, active_count, OptionsInvocationMode::k_start_menu),
      .width = k_options_row_width,
      .height = k_options_row_height};
  text.runtime_flags = k_options_text_flags;
  text.target_state = target_state;
  return text;
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
  quit->on_enter = [](InterfaceManager&, InterfaceInstance& instance_ref, I2DState& state_ref) {
    state_ref.selected_element = k_start_menu_quit_default_choice;
    instance_ref.current_state = &state_ref;
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
  options->on_enter =
      [](InterfaceManager& manager_ref, InterfaceInstance& instance_ref, I2DState& state_ref) {
        const InterfaceHandle host_handle{instance_ref.handle};
        instance_ref.current_state = &state_ref;

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
        }
      };

  // Animated background: IMAGES/CLOUD.BMP. Missing source degrades to no
  // background (the canvas stays clear) rather than an invented asset.
  if (auto background{I2DBumpBackground::create()}) {
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
                       .on_adjust = {},
                       .literal_text = {}},
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
                     .on_adjust = {},
                     .literal_text = {}},
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
            .on_adjust = {},
            .literal_text = {}},
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
// Phase 1 implements the real interface-35 lifecycle, resident START MENU
// hosting, dynamic Runtime row layout, root-page navigation and extensible row
// metadata. The four submenu bodies intentionally remain deferred to the
// subsequent phases rather than showing guessed controls.
void initialize_options(InterfaceManager& manager, InterfaceInstance& instance) {
  App::Log::debug(LogCategory::I2D, "initializing OPTIONS root state");

  I2DState* root{manager.create_state(instance)};
  I2DState* video_action{manager.create_state(instance)};
  I2DState* audio_action{manager.create_state(instance)};
  I2DState* game_action{manager.create_state(instance)};
  I2DState* controls_action{manager.create_state(instance)};
  I2DState* back_action{manager.create_state(instance)};

  if (root == nullptr || video_action == nullptr || audio_action == nullptr ||
      game_action == nullptr || controls_action == nullptr || back_action == nullptr) {
    App::Log::error(LogCategory::I2D, "failed to allocate the OPTIONS state graph");
    return;
  }

  const std::array<I2DState*, 5> targets{
      video_action, audio_action, game_action, controls_action, back_action};
  for (I2DState* target : targets) {
    target->parent = root;
  }

  const std::array<std::string_view, 4> deferred_pages{"video", "audio", "game", "controls"};
  for (std::size_t index{0}; index < deferred_pages.size(); ++index) {
    const std::string_view page_id{deferred_pages.at(index)};
    targets.at(index)->on_enter = [page_id](InterfaceManager&, InterfaceInstance&, I2DState&) {
      App::Log::info(LogCategory::Interface,
          "OPTIONS page \"{}\" is deferred to a later implementation phase",
          page_id);
    };
  }

  back_action->on_enter =
      [](InterfaceManager& manager_ref, InterfaceInstance& instance_ref, I2DState&) {
        close_options_to_host(manager_ref, instance_ref);
      };
  root->on_cancel = [](InterfaceManager& manager_ref, InterfaceInstance& instance_ref, I2DState&) {
    close_options_to_host(manager_ref, instance_ref);
  };

  // Root submenu/back rows are Runtime type 2/type 6 and use SNEAK.FNT.
  if (auto result{manager.load_font(k_options_root_font_key)}; !result) {
    App::Log::warn(LogCategory::I2D,
        "font '{}' unavailable for OPTIONS: {}",
        k_options_root_font_key,
        result.error());
  }

  I2DGroup root_rows;
  root_rows.runtime_flags = k_options_text_flags;
  std::size_t index{0};
  for (const OptionsRowDefinition& row : k_options_root_page.rows) {
    root_rows.elements.push_back(I2DElement{
        .data = make_options_text(row, index, k_options_root_page.rows.size(), targets.at(index)),
        .presentation = I2DPresentationHints{}});
    ++index;
  }
  root->groups.push_back(std::move(root_rows));
  root->selected_element = 0U;

  instance.root_state = root;
  instance.current_state = root;

  App::Log::debug(LogCategory::I2D,
      "OPTIONS root: {} selectable rows; active=\"{}\"",
      k_options_root_page.rows.size(),
      instance.strings.at(
          static_cast<std::uint16_t>(k_options_root_page.rows.front().runtime_string_index)));
}

// Runtime OPTIONS destroy callback: 0x00490F30. Resource release remains RAII.
void destroy_options(
    [[maybe_unused]] InterfaceManager& manager, [[maybe_unused]] InterfaceInstance& instance) {
  App::Log::debug(LogCategory::I2D, "destroying OPTIONS state");
}

}  // namespace

}  // namespace App::Interface
