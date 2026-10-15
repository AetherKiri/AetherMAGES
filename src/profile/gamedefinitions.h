#pragma once

#include <string>
#include <ankerl/unordered_dense.h>
#include "../util.h"

namespace Impacto::Profile {

struct GameDefinition {
  std::string GameProfile;
  std::string Name;
  ankerl::unordered_dense::map<std::string, std::string, string_hash,
                               std::equal_to<>>
      Patch;
  bool Hidden = false;
  int LauncherOrderId = 99;
  uint32_t LauncherTheme = 0xFFFFFF;
  // The game's profile mounts its archives from the game root itself
  // (PC digital-distribution layout: script.mpk/system.mpk at the root)
  // instead of the standard <gamedata>/<game-id>/ install layout. Used by
  // the host bridge's "auto" game resolution.
  bool PcFlatLayout = false;

  static void Configure();
};

inline ankerl::unordered_dense::map<std::string, GameDefinition, string_hash,
                                    std::equal_to<>>
    GameDefinitions;
}  // namespace Impacto::Profile