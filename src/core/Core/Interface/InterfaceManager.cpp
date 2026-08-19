#include "Core/Interface/InterfaceManager.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Input/InputAction.hpp"
#include "Core/Input/InputManager.hpp"
#include "Core/Interface/I2DBumpBackground.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Interface/I2DRenderer.hpp"
#include "Core/Interface/InterfaceDescriptor.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Interface/StartMenuLayout.hpp"
#include "Core/Log.hpp"
#include "Core/Omikron/BmpImage.hpp"
#include "Core/Omikron/IamStringTable.hpp"
#include "Core/Resources.hpp"
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

  const std::filesystem::path root_relative{Resources::game_data_path(
      std::filesystem::path{relative_path})};
  const std::filesystem::path resolved{Resources::resolve_case_insensitive(root_relative)};

  std::size_t size{0};
  void* raw{SDL_LoadFile(resolved.string().c_str(), &size)};
  if (raw == nullptr) {
    return std::expected<std::vector<std::byte>, std::string>{std::unexpect,
        fmt::format("cannot read '{}' (resolved '{}'): {}",
            relative_path,
            resolved.string(),
            SDL_GetError())};
  }

  std::vector<std::byte> bytes(size);
  if (size > 0) {
    std::memcpy(bytes.data(), raw, size);
  }
  SDL_free(raw);
  return bytes;
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
    return std::expected<InterfaceHandle, std::string>{std::unexpect,
        fmt::format("interface {} is unsupported", interface_id)};
  }

  close();

  App::Log::info("[I2D] opening interface {} \"{}\"", descriptor->id, descriptor->name);

  if (!m_renderer) {
    m_renderer = std::make_unique<I2DRenderer>();
    if (auto result{m_renderer->initialize()}; !result) {
      m_renderer.reset();
      return std::expected<InterfaceHandle, std::string>{std::unexpect,
          fmt::format("[I2D] renderer: {}", result.error())};
    }
  }

  InterfaceInstance instance;
  instance.descriptor = descriptor;
  instance.open_request = request;

  // Descriptor bitmap resource (I2D/bitmaps/<name>).
  if (!descriptor->bitmap_name.empty()) {
    const std::string path{bitmap_path(descriptor->bitmap_name)};
    auto file{read_file(path)};
    if (!file) {
      return std::expected<InterfaceHandle, std::string>{
          std::unexpect, fmt::format("[I2D] bitmap: {}", file.error())};
    }
    auto bmp{Omikron::BmpImageDecoder::load(std::span<const std::byte>{file.value()})};
    if (!bmp) {
      return std::expected<InterfaceHandle, std::string>{
          std::unexpect, fmt::format("[I2D] bitmap: {}", bmp.error())};
    }
    auto texture{Texture2D::create(
        bmp->width, bmp->height, std::span<const std::uint8_t>{bmp->rgba8}, /*srgb=*/true)};
    if (!texture) {
      return std::expected<InterfaceHandle, std::string>{
          std::unexpect, fmt::format("[I2D] bitmap: {}", texture.error())};
    }
    instance.bitmap.emplace(std::move(texture).value());
    App::Log::info("[I2D] bitmap: {} ({}x{})", path, bmp->width, bmp->height);
  }

  // Descriptor string-table resource (IAM/<name>).
  if (!descriptor->string_table_name.empty()) {
    const std::string path{string_table_path(descriptor->string_table_name)};
    auto file{read_file(path)};
    if (!file) {
      return std::expected<InterfaceHandle, std::string>{
          std::unexpect, fmt::format("[I2D] strings: {}", file.error())};
    }
    auto table{Omikron::IamStringTable::load(std::span<const std::byte>{file.value()})};
    if (!table) {
      return std::expected<InterfaceHandle, std::string>{
          std::unexpect, fmt::format("[I2D] strings: {}", table.error())};
    }
    const std::size_t entry_count{table->size()};
    instance.strings = std::move(table).value();
    App::Log::info("[I2D] strings: {} ({} entries)", path, entry_count);
  }

  m_instance.emplace(std::move(instance));

  // Interface-specific initialization builds the I2D state graph on top of
  // the now-loaded resources.
  if (descriptor->init != nullptr) {
    descriptor->init(*this, *m_instance);
  }

  if (m_instance->root_state == nullptr || m_instance->current_state == nullptr) {
    const std::string error{"interface initializer did not establish a root state"};
    App::Log::error("[I2D] {}", error);
    m_instance.reset();
    return std::expected<InterfaceHandle, std::string>{std::unexpect, error};
  }

  ++m_generation;
  m_instance->handle = InterfaceHandle{.interface_id = interface_id, .generation = m_generation};
  const InterfaceHandle handle{m_instance->handle};
  App::Log::info("[Interface] opened handle id={} gen={} \"{}\"",
      handle.interface_id,
      handle.generation,
      descriptor->name);
  return handle;
}

void InterfaceManager::close() {
  APP_PROFILE_FUNCTION();

  if (!m_instance.has_value()) {
    return;
  }
  const InterfaceDescriptor* descriptor{m_instance->descriptor};
  if (descriptor != nullptr && descriptor->destroy != nullptr) {
    descriptor->destroy(*this, *m_instance);
  }
  App::Log::info("[I2D] closing interface {} \"{}\"",
      descriptor != nullptr ? descriptor->id : -1,
      descriptor != nullptr ? descriptor->name : "?");
  App::Log::info("[Interface] closed handle id={} gen={}",
      m_instance->handle.interface_id,
      m_instance->handle.generation);
  m_instance.reset();
}

void InterfaceManager::request_completion(const std::int16_t result) {
  if (!m_instance.has_value()) {
    return;
  }
  m_pending_completion =
      InterfaceCompletion{.handle = m_instance->handle, .result = result};
  App::Log::info("[Interface] completion requested handle id={} gen={} result={}",
      m_instance->handle.interface_id,
      m_instance->handle.generation,
      result);
}

std::optional<InterfaceCompletion> InterfaceManager::take_completion() {
  std::optional<InterfaceCompletion> result{m_pending_completion};
  m_pending_completion.reset();
  return result;
}

void InterfaceManager::update(const float delta_time, const Input::InputManager& input) {
  APP_PROFILE_FUNCTION();

  if (!m_instance.has_value()) {
    return;
  }
  if (m_instance->current_state != nullptr && m_instance->current_state->background != nullptr) {
    m_instance->current_state->background->update(delta_time);
  }
  handle_navigation(input);
}

void InterfaceManager::render(const int pixel_width, const int pixel_height) {
  APP_PROFILE_FUNCTION();

  if (!m_instance.has_value() || m_renderer == nullptr) {
    return;
  }
  m_renderer->render(*m_instance, m_fonts, pixel_width, pixel_height);
}

I2DState* InterfaceManager::create_state() {
  if (!m_instance.has_value()) {
    return nullptr;
  }
  auto state{std::make_unique<I2DState>()};
  I2DState* raw{state.get()};
  m_instance->states.push_back(std::move(state));
  return raw;
}

std::expected<void, std::string> InterfaceManager::load_font(const char key) {
  return m_fonts.load_font(key);
}

void InterfaceManager::select_previous() {
  if (!m_instance.has_value() || m_instance->current_state == nullptr) {
    return;
  }
  std::vector<I2DTextElement*> selectable{selectable_text_elements(*m_instance->current_state)};
  if (selectable.empty()) {
    return;
  }
  if (m_instance->selected_element == 0U) {
    m_instance->selected_element = selectable.size() - 1U;
  } else {
    --m_instance->selected_element;
  }
  App::Log::info("[I2D] active element: string[{}] \"{}\"",
      selectable.at(m_instance->selected_element)->string_index,
      m_instance->strings.at(selectable.at(m_instance->selected_element)->string_index));
}

void InterfaceManager::select_next() {
  if (!m_instance.has_value() || m_instance->current_state == nullptr) {
    return;
  }
  std::vector<I2DTextElement*> selectable{selectable_text_elements(*m_instance->current_state)};
  if (selectable.empty()) {
    return;
  }
  m_instance->selected_element = (m_instance->selected_element + 1U) % selectable.size();
  App::Log::info("[I2D] active element: string[{}] \"{}\"",
      selectable.at(m_instance->selected_element)->string_index,
      m_instance->strings.at(selectable.at(m_instance->selected_element)->string_index));
}

void InterfaceManager::confirm() {
  if (!m_instance.has_value() || m_instance->current_state == nullptr) {
    return;
  }
  std::vector<I2DTextElement*> selectable{selectable_text_elements(*m_instance->current_state)};
  if (selectable.empty()) {
    return;
  }
  I2DTextElement* selected{selectable.at(m_instance->selected_element)};
  I2DState* target{selected->target_state};
  if (target == nullptr) {
    return;  // Not selectable (should not happen for a selectable element).
  }
  App::Log::info("[I2D] activate: string[{}] \"{}\" -> child state",
      selected->string_index,
      m_instance->strings.at(selected->string_index));
  if (target->on_enter) {
    // A state-specific enter action (e.g. queueing an interface completion)
    // owns the transition; the generic path must not also switch states.
    target->on_enter(*this, *m_instance, *target);
    return;
  }
  // Generic child-state transition (no enter action).
  m_instance->current_state = target;
  m_instance->selected_element = 0U;
}

void InterfaceManager::cancel() {
  if (!m_instance.has_value() || m_instance->current_state == nullptr) {
    return;
  }
  if (m_instance->current_state->parent == nullptr) {
    App::Log::debug("[I2D] cancel: already at the root state");
    return;
  }
  m_instance->current_state = m_instance->current_state->parent;
  m_instance->selected_element = 0U;
  App::Log::info("[I2D] returned to parent state");
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
  App::Log::info("[I2D] initializing START MENU root state");

  I2DState* root{manager.create_state()};
  I2DState* new_game{manager.create_state()};
  I2DState* load_game{manager.create_state()};
  I2DState* options{manager.create_state()};
  I2DState* quit{manager.create_state()};
  if (root == nullptr || new_game == nullptr || load_game == nullptr || options == nullptr ||
      quit == nullptr) {
    App::Log::error("[I2D] failed to allocate the START MENU state graph");
    return;
  }

  new_game->parent = root;
  load_game->parent = root;
  options->parent = root;
  quit->parent = root;

  // New Game completes interface 29. The result value is provisional and
  // clearly documented; the important behavior is that the waiting AREA
  // script resumes (which starts track 87 through opcode 0x67). Track 87 is
  // never started directly here.
  new_game->on_enter = [](InterfaceManager& manager_ref, InterfaceInstance&, I2DState&) {
    manager_ref.request_completion(/*provisional result*/ 0);
  };

  // Animated background: IMAGES/CLOUD.BMP. Missing source degrades to no
  // background (the canvas stays clear) rather than an invented asset.
  if (auto background{I2DBumpBackground::create()}) {
    root->background = background->get();
    instance.background = std::move(background).value();
  } else {
    App::Log::warn("[I2D] background unavailable: {}", background.error());
  }

  // Font key 'I' -> MENUINTR (the FNT renderer falls back to the TTF).
  if (auto result{manager.load_font('I')}; !result) {
    App::Log::warn("[I2D] font 'I' unavailable: {}", result.error());
  }

  // Runtime bitmap element approximately 0x004CF1A8.
  // Source/destination: 0,0,640,150. Raw flags: 0x40000100.
  I2DGroup bitmap_group;
  bitmap_group.elements.push_back(I2DElement{I2DBitmapElement{
      .source = k_start_menu_bitmap_rect,
      .destination = k_start_menu_bitmap_rect,
      .runtime_flags = k_start_menu_bitmap_flags}});
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
    text_group.elements.push_back(I2DElement{I2DTextElement{.string_index = entry.string_index,
        .font_key = entry.font_key,
        .bounds = I2DRect{.x = entry.x, .y = entry.y, .width = entry.width, .height = entry.height},
        .red = 255,
        .green = 255,
        .blue = 255,
        .target_state = child_states.at(index)}});
  }
  root->groups.push_back(std::move(text_group));

  instance.root_state = root;
  instance.current_state = root;
  instance.selected_element = 0U;

  App::Log::info("[I2D] root state: 4 selectable text elements");
  App::Log::info("[I2D] active element: string[{}] \"{}\"", 0, instance.strings.at(0));
}

// Runtime StartMenu destroy callback: 0x00479F30. Resource release is RAII;
// this callback exists to mirror the descriptor contract and to log teardown.
void destroy_start_menu([[maybe_unused]] InterfaceManager& manager,
    [[maybe_unused]] InterfaceInstance& instance) {
  App::Log::info("[I2D] destroying START MENU state");
}

}  // namespace

}  // namespace App::Interface
