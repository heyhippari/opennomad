#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <expected>

#include "Core/Sprite/SpritePool.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

namespace {

using App::Sprite::SpriteHandle;
using App::Sprite::SpriteInstance;
using App::Sprite::SpritePool;
using App::Sprite::SpriteRenderMode;

constexpr std::size_t k_frame_count{3};

/// Creates one instance with the given frame count, or destroys the pool's
/// current handle when reuse is requested.
SpriteHandle spawn(SpritePool& pool, const std::size_t frame_count = k_frame_count) {
  return pool.create(0, 0, frame_count).value();
}

}  // namespace

TEST_SUITE("Core::Sprite::SpritePool") {
  TEST_CASE("create applies the Runtime defaults") {
    SpritePool pool;
    const SpriteHandle handle{spawn(pool)};
    REQUIRE(handle.valid());
    const SpriteInstance* instance{pool.find(handle)};
    REQUIRE(instance != nullptr);

    CHECK_EQ(instance->scale_x, 1.0F);
    CHECK_EQ(instance->scale_y, 1.0F);
    CHECK_EQ(instance->unknown_24, 0.9F);
    CHECK_EQ(instance->frame_index, SpriteInstance::k_invalid_frame);
    CHECK_EQ(instance->tint.at(0), 1.0F);
    CHECK_EQ(instance->tint.at(1), 1.0F);
    CHECK_EQ(instance->tint.at(2), 1.0F);
    CHECK_EQ(instance->render_mode, SpriteRenderMode::k_default);
    CHECK_EQ(instance->rotation, 0.0F);
    CHECK_EQ(instance->texture_offset_u, 0.0F);
    CHECK_EQ(instance->texture_offset_v, 0.0F);
    CHECK_EQ(instance->external_association, nullptr);
    CHECK_EQ(instance->render_list_owner, nullptr);
    CHECK_EQ(instance->position.at(0), 0.0F);
  }

  TEST_CASE("create applies the requested position") {
    SpritePool pool;
    const SpriteHandle handle{
        pool.create(0, 0, k_frame_count, {1.0F, 2.0F, 3.0F}).value()};
    const SpriteInstance* instance{pool.find(handle)};
    REQUIRE(instance != nullptr);
    CHECK_EQ(instance->position.at(0), 1.0F);
    CHECK_EQ(instance->position.at(1), 2.0F);
    CHECK_EQ(instance->position.at(2), 3.0F);
  }

  TEST_CASE("grows beyond the Runtime default capacity") {
    SpritePool pool;
    for (std::size_t index{0}; index < 2100; ++index) {
      const auto handle{pool.create(0, 0, k_frame_count)};
      REQUIRE(handle.has_value());
      CHECK(pool.find(*handle) != nullptr);
    }
    CHECK_GE(pool.capacity(), std::size_t{2100});
    CHECK_EQ(pool.live_count(), std::size_t{2100});
  }

  TEST_CASE("recycles slots with bumped generations") {
    SpritePool pool;
    const SpriteHandle first{spawn(pool)};
    REQUIRE(pool.destroy(first).has_value());

    const SpriteHandle second{spawn(pool)};
    CHECK_EQ(second.index, first.index);
    CHECK_NE(second.generation, first.generation);
    CHECK(pool.find(first) == nullptr);
    CHECK(pool.find(second) != nullptr);
  }

  TEST_CASE("stale handles fail safely") {
    SpritePool pool;
    const SpriteHandle handle{spawn(pool)};
    REQUIRE(pool.destroy(handle).has_value());

    CHECK(pool.find(handle) == nullptr);
    CHECK_FALSE(pool.attached(handle));
    CHECK_FALSE(pool.attach(handle).has_value());
    CHECK_FALSE(pool.detach(handle).has_value());
    CHECK_FALSE(pool.destroy(handle).has_value());
    CHECK_FALSE(pool.set_frame(handle, 0).has_value());
    pool.set_render_mode(handle, SpriteRenderMode::k_additive);  // Must not crash.
  }

  TEST_CASE("attach inserts at the head") {
    SpritePool pool;
    const SpriteHandle first{spawn(pool)};
    const SpriteHandle second{spawn(pool)};
    REQUIRE(pool.attach(first).has_value());
    REQUIRE(pool.attach(second).has_value());

    const auto head{pool.render_list_head()};
    REQUIRE(head.has_value());
    CHECK_EQ(*head, second);
    CHECK_EQ(pool.attached_count(), std::size_t{2});
    CHECK(pool.find(second)->render_list_owner != nullptr);

    const auto next{pool.render_list_next(*head)};
    REQUIRE(next.has_value());
    CHECK_EQ(*next, first);
    CHECK_FALSE(pool.render_list_next(*next).has_value());
  }

  TEST_CASE("attach rejects an already attached instance") {
    SpritePool pool;
    const SpriteHandle handle{spawn(pool)};
    REQUIRE(pool.attach(handle).has_value());
    const auto duplicate{pool.attach(handle)};
    CHECK_FALSE(duplicate.has_value());
    CHECK_EQ(pool.attached_count(), std::size_t{1});
  }

  TEST_CASE("detach repairs head, middle and tail links") {
    SpritePool pool;
    const SpriteHandle first{spawn(pool)};
    const SpriteHandle second{spawn(pool)};
    const SpriteHandle third{spawn(pool)};
    REQUIRE(pool.attach(first).has_value());
    REQUIRE(pool.attach(second).has_value());
    REQUIRE(pool.attach(third).has_value());
    // Render list: third -> second -> first.

    REQUIRE(pool.detach(second).has_value());  // Middle: third -> first.
    auto head{pool.render_list_head()};
    REQUIRE(head.has_value());
    CHECK_EQ(*head, third);
    const auto next{pool.render_list_next(*head)};
    REQUIRE(next.has_value());
    CHECK_EQ(*next, first);
    CHECK_FALSE(pool.render_list_next(*next).has_value());
    CHECK_EQ(pool.attached_count(), std::size_t{2});

    REQUIRE(pool.detach(third).has_value());  // Head: first only.
    head = pool.render_list_head();
    REQUIRE(head.has_value());
    CHECK_EQ(*head, first);
    CHECK_FALSE(pool.render_list_next(*head).has_value());

    REQUIRE(pool.detach(first).has_value());  // Tail: empty list.
    CHECK_FALSE(pool.render_list_head().has_value());
    CHECK_EQ(pool.attached_count(), std::size_t{0});
    CHECK(pool.find(first)->render_list_owner == nullptr);
  }

  TEST_CASE("detach rejects a detached instance") {
    SpritePool pool;
    const SpriteHandle handle{spawn(pool)};
    CHECK_FALSE(pool.detach(handle).has_value());
  }

  TEST_CASE("destroy detaches automatically") {
    SpritePool pool;
    const SpriteHandle handle{spawn(pool)};
    REQUIRE(pool.attach(handle).has_value());
    REQUIRE(pool.destroy(handle).has_value());
    CHECK_EQ(pool.attached_count(), std::size_t{0});
    CHECK_FALSE(pool.render_list_head().has_value());
    CHECK(pool.find(handle) == nullptr);
    CHECK_EQ(pool.live_count(), std::size_t{0});
  }

  TEST_CASE("destroy of a detached instance succeeds") {
    SpritePool pool;
    const SpriteHandle handle{spawn(pool)};
    REQUIRE(pool.destroy(handle).has_value());
    CHECK_EQ(pool.live_count(), std::size_t{0});
  }

  TEST_CASE("set_frame accepts the full frame range and rejects beyond it") {
    SpritePool pool;
    const SpriteHandle handle{spawn(pool)};

    REQUIRE(pool.set_frame(handle, 0).has_value());
    CHECK_EQ(pool.find(handle)->frame_index, 0U);
    REQUIRE(pool.set_frame(handle, 2).has_value());  // Last valid frame.
    CHECK_EQ(pool.find(handle)->frame_index, 2U);

    const auto out_of_range{pool.set_frame(handle, 3)};
    CHECK_FALSE(out_of_range.has_value());
    CHECK_EQ(pool.find(handle)->frame_index, SpriteInstance::k_invalid_frame);

    CHECK_FALSE(pool.set_frame(handle, SpriteInstance::k_invalid_frame).has_value());
  }

  TEST_CASE("set_render_mode is a direct assignment that always succeeds") {
    SpritePool pool;
    const SpriteHandle handle{spawn(pool)};
    pool.set_render_mode(handle, SpriteRenderMode::k_darken_cutout);
    CHECK_EQ(pool.find(handle)->render_mode, SpriteRenderMode::k_darken_cutout);
    pool.set_render_mode(SpriteHandle{}, SpriteRenderMode::k_alpha);  // Stale: no-op.
  }

  TEST_CASE("reset_to_defaults restores the Runtime values") {
    SpritePool pool;
    const SpriteHandle handle{spawn(pool)};
    pool.set_scale(handle, 3.0F, 4.0F);
    pool.set_rotation(handle, 1.0F);
    pool.set_tint(handle, {0.5F, 0.25F, 0.125F});
    pool.set_render_mode(handle, SpriteRenderMode::k_additive);
    pool.set_texture_offset(handle, 0.25F, 0.5F);
    pool.set_unknown_24(handle, 7.0F);
    REQUIRE(pool.set_frame(handle, 1).has_value());
    pool.set_position(handle, {9.0F, 8.0F, 7.0F});

    pool.reset_to_defaults(handle);
    const SpriteInstance* instance{pool.find(handle)};
    REQUIRE(instance != nullptr);
    CHECK_EQ(instance->scale_x, 1.0F);
    CHECK_EQ(instance->scale_y, 1.0F);
    CHECK_EQ(instance->rotation, 0.0F);
    CHECK_EQ(instance->unknown_24, 0.9F);
    CHECK_EQ(instance->frame_index, SpriteInstance::k_invalid_frame);
    CHECK_EQ(instance->render_mode, SpriteRenderMode::k_default);
    CHECK_EQ(instance->texture_offset_u, 0.0F);
    CHECK_EQ(instance->texture_offset_v, 0.0F);
    CHECK_EQ(instance->tint.at(0), 1.0F);
    CHECK_EQ(instance->position.at(0), 9.0F);  // Position is preserved.
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
