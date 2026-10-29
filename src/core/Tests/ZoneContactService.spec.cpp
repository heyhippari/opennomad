#include <doctest/doctest.h>

#include <array>
#include <cstdint>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include "Core/Omikron/IamZone.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Scenario/TriggerProxyService.hpp"
#include "Core/Scenario/ZoneContactService.hpp"

namespace {

App::Omikron::IamZoneRecord make_zone() {
  App::Omikron::IamZoneRecord zone;
  zone.zone_id = 12;
  zone.event_offsets = {0x10U, 0U, 0x20U};
  zone.serialized_vertices = {{{0, 900, 0}, {100, 1100, 0}, {100, 1100, 100}, {0, 900, 100}}};
  return zone;
}

/// A ready proxy standing at the centre of make_zone()'s footprint and vertical slab.
App::CurrentCharacterTriggerProxy make_ready_proxy(const App::Omikron::IamZoneRecord& zone) {
  const App::Runtime::AreaZoneSpatialBounds bounds{
      App::Runtime::area_zone_spatial_bounds(zone.serialized_vertices)};
  return {.owner = {},
      .registered = true,
      .contact_ready = true,
      .position = {.x = (bounds.minimum.x + bounds.maximum.x) * 0.5F,
          .y = (bounds.minimum.y + bounds.maximum.y) * 0.5F,
          .z = (bounds.minimum.z + bounds.maximum.z) * 0.5F},
      .radius = 1.0F};
}

}  // namespace

TEST_SUITE("Core::Scenario::ZoneContactService") {
  TEST_CASE("zone identity distinguishes slot, source, ids and event entries") {
    const App::ActiveZoneRef base{.resident_slot = 0U,
        .source = App::ActiveZoneSource::k_area,
        .area_id = 118,
        .scene_id = -1,
        .zone = make_zone()};
    CHECK_EQ(base.identity(), base.identity());

    App::ActiveZoneRef other_slot{base};
    other_slot.resident_slot = 1U;
    CHECK_NE(base.identity(), other_slot.identity());

    App::ActiveZoneRef other_source{base};
    other_source.source = App::ActiveZoneSource::k_scene;
    CHECK_NE(base.identity(), other_source.identity());

    App::ActiveZoneRef other_scene{base};
    other_scene.scene_id = 55;
    CHECK_NE(base.identity(), other_scene.identity());

    // Same ZONE ID but different authored event entries is a different record.
    App::ActiveZoneRef other_events{base};
    other_events.zone.event_offsets.at(0) = 0x99U;
    CHECK_NE(base.identity(), other_events.identity());

    App::ZoneContactContext contact;
    contact.resident_slot = base.resident_slot;
    contact.source = base.source;
    contact.area_id = base.area_id;
    contact.scene_id = base.scene_id;
    contact.zone = base.zone;
    CHECK_EQ(contact.identity(), base.identity());
  }

  TEST_CASE("spatial qualification needs a registered, contact-ready proxy") {
    const App::Omikron::IamZoneRecord zone{make_zone()};
    App::CurrentCharacterTriggerProxy proxy{make_ready_proxy(zone)};

    CHECK(App::evaluate_zone_spatial_qualification(&proxy, zone).qualifies());
    CHECK_FALSE(App::evaluate_zone_spatial_qualification(nullptr, zone).qualifies());

    proxy.contact_ready = false;
    CHECK_FALSE(App::evaluate_zone_spatial_qualification(&proxy, zone).broadphase);
    proxy.contact_ready = true;
    proxy.registered = false;
    CHECK_FALSE(App::evaluate_zone_spatial_qualification(&proxy, zone).qualifies());
  }

  TEST_CASE("spatial qualification applies broadphase, polygon and heading independently") {
    App::Omikron::IamZoneRecord zone{make_zone()};
    App::CurrentCharacterTriggerProxy proxy{make_ready_proxy(zone)};

    proxy.position.x += 100000.0F;
    const App::ZoneSpatialQualification far_away{
        App::evaluate_zone_spatial_qualification(&proxy, zone)};
    CHECK_FALSE(far_away.broadphase);
    CHECK_FALSE(far_away.polygon);
    CHECK_FALSE(far_away.qualifies());

    proxy = make_ready_proxy(zone);
    zone.orientation_center_units = 0;
    zone.orientation_span_units = 256;  // 22.5 degrees wide, centred on 0.
    proxy.heading_degrees = 90.0F;
    const App::ZoneSpatialQualification wrong_heading{
        App::evaluate_zone_spatial_qualification(&proxy, zone)};
    CHECK(wrong_heading.broadphase);
    CHECK(wrong_heading.polygon);
    CHECK_FALSE(wrong_heading.heading);
    CHECK_FALSE(wrong_heading.qualifies());

    proxy.heading_degrees = 5.0F;
    CHECK(App::evaluate_zone_spatial_qualification(&proxy, zone).qualifies());
  }

  TEST_CASE("recovered spatial-contact capacity is sixteen registrations") {
    CHECK_EQ(App::ZoneContactService::k_max_registrations, 16U);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)
