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
}  // namespace

const InterfaceDescriptor* descriptor_for_id(const std::int32_t id) {
  // Static registry mirroring Runtime's interface-descriptor table. Only
  // interface 29 is implemented; neighbours (28 DIVERS, 30 SAVE GAME,
  // 31 PAUSE GAME, 35 OPTIONS, 36 HIGH-SCORE) are documented for the next
  // milestone. Descriptor #29 metadata recovered from Runtime @ 0x004CC0AC.
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

void InterfaceManager::render_dialog(const Dialog::DialogPresentation& dialog,
    const std::size_t selected_choice,
    const int pixel_width,
    const int pixel_height) {
  if (m_renderer == nullptr) {
    return;
  }
  Debug::I2DCounters counters;
  m_renderer->render_dialog(dialog, selected_choice, m_fonts, pixel_width, pixel_height, counters);
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
  App::Log::debug(LogCategory::I2D,
      "active element: string[{}] \"{}\"",
      selectable.at(selected)->string_index,
      instance->strings.at(selectable.at(selected)->string_index));
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
  App::Log::debug(LogCategory::I2D,
      "active element: string[{}] \"{}\"",
      selectable.at(selected)->string_index,
      instance->strings.at(selectable.at(selected)->string_index));
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
  I2DTextElement* selected{selectable.at(instance->current_state->selected_element)};
  I2DState* target{selected->target_state};
  if (target == nullptr) {
    return;  // Not selectable (should not happen for a selectable element).
  }
  App::Log::debug(LogCategory::I2D,
      "activate: string[{}] \"{}\" -> child state",
      selected->string_index,
      instance->strings.at(selected->string_index));
  if (target->on_enter) {
    // A state-specific enter action (e.g. queueing an interface completion)
    // owns the transition; the generic path must not also switch states.
    target->on_enter(*this, *instance, *target);
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
  if (input.is_action_pressed(Input::Action::k_menu_confirm)) {
    confirm();
  }
  if (input.is_action_pressed(Input::Action::k_menu_cancel)) {
    cancel();
  }
}

namespace {

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

  // Animated background: IMAGES/CLOUD.BMP. Missing source degrades to no
  // background (the canvas stays clear) rather than an invented asset.
  if (auto background{I2DBumpBackground::create()}) {
    root->background = background->get();
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
                       .target_state = child_states.at(index)},
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
                     .target_state = nullptr},
          .presentation = I2DPresentationHints{}});
  quit->groups.push_back(std::move(quit_title_group));

  // Runtime confirmation selector:
  //   IAM/Menu[6] = "Yes" -> callback 0x0047BC10 -> PostQuitMessage(0)
  //   IAM/Menu[7] = "No"  -> parent/root state 0x004CF218
  //
  // Both recovered elements intentionally share the same 640x40 rectangle;
  // the group's selected member is the one presented.
  const std::array<I2DState*, 2> quit_targets{quit_yes_action, root};
  I2DGroup quit_choice_group;
  quit_choice_group.runtime_flags = k_start_menu_text_group_flags;
  quit_choice_group.render_selected_only = true;
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
            .target_state = quit_targets.at(index)},
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

}  // namespace

}  // namespace App::Interface
