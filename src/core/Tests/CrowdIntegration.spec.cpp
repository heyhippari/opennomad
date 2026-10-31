#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Crowd/CrowdRuntime.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/IamArchive.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/Model3DO.hpp"

namespace {

App::Omikron::IamAreaRecord load_area(const std::uint32_t area_id) {
  const auto archive_file{App::load_game_file("IAM/AREA")};
  REQUIRE(archive_file.has_value());
  const App::Omikron::IamIndexedArchive archive{archive_file->bytes};
  const auto record{archive.read_record(area_id)};
  REQUIRE(record.has_value());
  auto area{App::Omikron::IamAreaRecord::load(record.value())};
  REQUIRE(area.has_value());
  return std::move(area).value();
}

}  // namespace

TEST_CASE("[RETAIL] Anekbah populates its passerby crowd") {
  const App::Omikron::IamAreaRecord area{load_area(0U)};
  CHECK(area.male_passer_variants() == 0x201U);
  CHECK(area.female_passer_variants() == 0x41U);

  auto population{App::Crowd::load_area_population(
      area, App::Crowd::K_DEFAULT_STREET_ACTIVITY, App::Character::Runtime::load_model_resource)};
  REQUIRE(population.has_value());
  REQUIRE(population->has_value());
  App::Crowd::Runtime crowd;
  REQUIRE(crowd.populate(std::move(population.value().value())).has_value());

  // PSH_FN, PSH1_FN, FSH_FN and FSH1_FN, each with four 19-object levels,
  // then two `sli_fn` variants and `moto`.
  REQUIRE(crowd.prototypes().size() == 7U);
  CHECK(area.slider_variants() == 0x3U);
  CHECK(area.moto_variants() == 0x1U);
  for (const App::Crowd::Prototype& prototype : crowd.prototypes().first(4U)) {
    CHECK(prototype.level_roots.size() == 4U);
    for (const auto& objects : prototype.level_objects) {
      CHECK(objects.size() == 19U);
    }
  }
  CHECK(crowd.prototypes()
            .front()
            .resource->model.meshes.at(crowd.prototypes().front().level_roots.front())
            .name == "PhBassin");
  CHECK(crowd.agents().size() == App::Crowd::K_MAX_PEDESTRIANS + App::Crowd::K_MAX_SLIDERS);
  const App::Crowd::Prototype& slider{crowd.prototypes()[4]};
  CHECK(slider.first_level == 1U);
  CHECK(slider.resource->model.meshes.at(slider.level_roots.front()).name == "SlBassin");
  CHECK(slider.resource->model.meshes.at(slider.level_roots.at(1)).name == "slider_fl");
  CHECK(crowd.agents()[App::Crowd::K_MAX_PEDESTRIANS].boardable);

  for (int tick{0}; tick < 300; ++tick) {
    crowd.tick(1.0F);
  }
  for (const App::Crowd::Agent& agent : crowd.agents()) {
    if (agent.kind == App::Crowd::AgentKind::k_passer) {
      CHECK(crowd.animation(agent) != nullptr);
    } else {
      CHECK(std::isfinite(agent.position.x));
      CHECK(agent.nominal_speed <= 5120.0F);
    }
  }
}

TEST_CASE("[RETAIL] passerby characters present only their highest level of detail") {
  const auto resource{App::Character::Runtime::load_model_resource("PSH_FN")};
  REQUIRE(resource.has_value());
  const App::Omikron::Model3DOData& model{resource.value()->model};
  REQUIRE(resource.value()->actor_object_index.has_value());
  const std::size_t actor{resource.value()->actor_object_index.value()};
  CHECK(model.meshes.at(actor).name == "PhBassin");
  const auto presented{App::Omikron::Model3DO::subtree_preorder(model, actor)};
  const std::set<std::size_t> allowed{presented.begin(), presented.end()};
  REQUIRE_FALSE(resource.value()->posed_layout.groups.empty());
  for (const App::Omikron::MaterialGroup& group : resource.value()->posed_layout.groups) {
    CHECK(allowed.contains(group.mesh_index));
  }
}
