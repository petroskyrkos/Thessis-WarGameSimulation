#include "entities/UnitProfile.h"

#include "config/Config.h"

namespace wargame {

UnitProfile UnitProfile::load(const Config& cfg, const std::string& path) {
    UnitProfile p;
    p.name = cfg.req<std::string>(path + ".name");
    p.hp = cfg.req<double>(path + ".hp");
    p.attack = cfg.req<double>(path + ".attack");
    p.hitProbability = cfg.req<double>(path + ".hit_probability");
    p.ewCoverage = cfg.req<double>(path + ".ew_coverage");
    p.attackRangeBoxes = cfg.req<std::int32_t>(path + ".attack_range_boxes");
    p.sensingRangeBoxes = cfg.req<std::int32_t>(path + ".sensing_range_boxes");
    p.speedKmh = cfg.req<double>(path + ".speed_kmh");
    p.minSpeedFraction = cfg.req<double>(path + ".min_speed_fraction");

    p.capacity[resourceIndex(Resource::Munitions)] =
        cfg.req<double>(path + ".capacity.munitions");
    p.capacity[resourceIndex(Resource::Fuel)] = cfg.req<double>(path + ".capacity.fuel");
    p.capacity[resourceIndex(Resource::Supplies)] =
        cfg.req<double>(path + ".capacity.supplies");

    p.basePerDay[resourceIndex(Resource::Munitions)] =
        cfg.req<double>(path + ".base_per_day.munitions");
    p.basePerDay[resourceIndex(Resource::Fuel)] =
        cfg.req<double>(path + ".base_per_day.fuel");
    p.basePerDay[resourceIndex(Resource::Supplies)] =
        cfg.req<double>(path + ".base_per_day.supplies");

    p.fuelPerBox = cfg.req<double>(path + ".fuel_per_box");
    p.munitionsPerEngagement =
        cfg.req<double>(path + ".munitions_per_engagement");

    if (p.hp <= 0.0) {
        throw ConfigError(path + ".hp must be positive");
    }
    if (p.hitProbability < 0.0 || p.hitProbability > 1.0) {
        throw ConfigError(path + ".hit_probability must be between 0 and 1");
    }
    if (p.ewCoverage < 0.0 || p.ewCoverage > 1.0) {
        throw ConfigError(path + ".ew_coverage must be between 0 and 1");
    }
    if (p.minSpeedFraction <= 0.0 || p.minSpeedFraction > 1.0) {
        throw ConfigError(path + ".min_speed_fraction must be in (0, 1]");
    }
    return p;
}

}  // namespace wargame
