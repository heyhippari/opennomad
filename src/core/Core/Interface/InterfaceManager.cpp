#include "Core/Interface/InterfaceManager.hpp"

#include <fmt/format.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/Input/InputAction.hpp"
#include "Core/Input/InputManager.hpp"
#include "Core/Interface/I2DBumpBackground.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Interface/I2DRenderer.hpp"
#include "Core/Interface/InterfaceDescriptor.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
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
          .runtime_flags = 0x20000400},
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

  App::Log::debug(LogCategory::I2D, "opening interface {} \"{}\"", descriptor->id, descriptor->name);

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
        /*srgb=*/true,
        TextureFilter::k_linear)};
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
  const InterfaceInstance* instance{find(handle)};
  if (instance == nullptr) {
    return;
  }
  m_completions.push_back(InterfaceCompletion{.handle = handle, .result = result});
  App::Log::info(LogCategory::Interface,
      "completed {} \"{}\" — handle={}:{} result={}",
      handle.interface_id,
      instance->descriptor->name,
      handle.interface_id,
      handle.generation,
      result);
}

std::optional<InterfaceCompletion> InterfaceManager::take_completion() {
  if (m_completions.empty()) {
    return std::nullopt;
  }
  InterfaceCompletion completion{m_completions.front()};
  m_completions.pop_front();
  return completion;
}

void InterfaceManager::update(const float delta_time, const Input::InputManager& input) {
  APP_PROFILE_FUNCTION();

  for (const auto& instance : m_instances) {
    if (instance->current_state != nullptr && instance->current_state->background != nullptr) {
      instance->current_state->background->update(delta_time);
    }
  }
  handle_navigation(input);
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
  Debug::Metrics::get().set_i2d_counters(counters);
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
  InterfaceInstance* instance{focused_instance_mut()};
  if (instance == nullptr || instance->current_state == nullptr) {
    return;
  }
  std::vector<I2DTextElement*> selectable{selectable_text_elements(*instance->current_state)};
  if (selectable.empty()) {
    return;
  }
  if (instance->selected_element == 0U) {
    instance->selected_element = selectable.size() - 1U;
  } else {
    --instance->selected_element;
  }
  App::Log::debug(LogCategory::I2D,
      "active element: string[{}] \"{}\"",
      selectable.at(instance->selected_element)->string_index,
      instance->strings.at(selectable.at(instance->selected_element)->string_index));
}

void InterfaceManager::select_next() {
  InterfaceInstance* instance{focused_instance_mut()};
  if (instance == nullptr || instance->current_state == nullptr) {
    return;
  }
  std::vector<I2DTextElement*> selectable{selectable_text_elements(*instance->current_state)};
  if (selectable.empty()) {
    return;
  }
  instance->selected_element = (instance->selected_element + 1U) % selectable.size();
  App::Log::debug(LogCategory::I2D,
      "active element: string[{}] \"{}\"",
      selectable.at(instance->selected_element)->string_index,
      instance->strings.at(selectable.at(instance->selected_element)->string_index));
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
  I2DTextElement* selected{selectable.at(instance->selected_element)};
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
  instance->selected_element = 0U;
}

void InterfaceManager::cancel() {
  InterfaceInstance* instance{focused_instance_mut()};
  if (instance == nullptr || instance->current_state == nullptr) {
    return;
  }
  if (instance->current_state->parent == nullptr) {
    App::Log::debug(LogCategory::I2D, "cancel: already at the root state");
    return;
  }
  instance->current_state = instance->current_state->parent;
  instance->selected_element = 0U;
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
  if (root == nullptr || new_game == nullptr || load_game == nullptr || options == nullptr ||
      quit == nullptr) {
    App::Log::error(LogCategory::I2D, "failed to allocate the START MENU state graph");
    return;
  }

  new_game->parent = root;
  load_game->parent = root;
  options->parent = root;
  quit->parent = root;

  // New Game completes this interface instance. The result value is
  // provisional and clearly documented; the important behavior is that the
  // waiting AREA script resumes (which starts track 87 through opcode 0x67).
  // Track 87 is never started directly here.
  new_game->on_enter =
      [](InterfaceManager& manager_ref, InterfaceInstance& instance_ref, I2DState&) {
        manager_ref.request_completion(instance_ref.handle, /*provisional result*/ 0);
      };

  // Animated background: IMAGES/CLOUD.BMP. Missing source degrades to no
  // background (the canvas stays clear) rather than an invented asset.
  if (auto background{I2DBumpBackground::create()}) {
    root->background = background->get();
    instance.background = std::move(background).value();
  } else {
    App::Log::warn(LogCategory::I2D, "background unavailable: {}", background.error());
  }

  // Font key 'I' -> MENUINTR (the FNT renderer falls back to the TTF).
  if (auto result{manager.load_font('I')}; !result) {
    App::Log::warn(LogCategory::I2D, "font 'I' unavailable: {}", result.error());
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

  instance.root_state = root;
  instance.current_state = root;
  instance.selected_element = 0U;

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
