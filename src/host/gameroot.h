#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Impacto::Host {
inline std::string FoldName(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(),
      [](unsigned char c) { return char(std::tolower(c)); });
  return name;
}
struct GameRoot {
  std::string Id;
  std::filesystem::path Archives;
};
// Directory names resolve the profiles whose archive families are identical.
// Unnamed roots need a distinguishing archive so unrelated MPK games cannot
// silently boot with an incompatible VM profile.
inline std::optional<GameRoot> DetectGameRoot(const std::filesystem::path& root) {
  namespace fs = std::filesystem;
  struct Profile { const char* Id; const char* Script; const char* System; const char* Unique; };
  constexpr std::array<Profile, 7> profiles{{
      {"chlcc","script.cpk","system.cpk","script.cpk"},
      {"darling","script.cls","system.cpk",nullptr},
      {"rne","script.cls","system.cpk","model.cpk"},
      {"cclcc","script","system.mpk","bgeffect.mpk"},
      {"dash","script.cls","system.cls","model.cls"},
      {"mo6tw","script.cls","system.cpk",nullptr},
      {"mo8","script.mpk","system.mpk","bg1.mpk"},
  }};
  auto inspect = [&](const fs::path& dir) -> std::optional<GameRoot> {
    std::error_code ec;
    std::vector<std::string> files;
    for (fs::directory_iterator it(dir,ec), end; !ec && it != end; it.increment(ec)) {
      std::error_code typeEc;
      if (it->is_regular_file(typeEc) || it->is_directory(typeEc))
        files.push_back(FoldName(it->path().filename().string()));
    }
    if (ec) return std::nullopt;
    auto has = [&](const char* name) { return std::find(files.begin(),files.end(),name) != files.end(); };
    std::string name = FoldName(dir.filename().string());
    for (const auto& p : profiles) {
      if (name == p.Id && (has(p.Script) || has(p.System))) return GameRoot{p.Id,dir};
    }
    for (const auto& p : profiles) {
      if (p.Unique && has(p.Script) && has(p.System) && has(p.Unique)) return GameRoot{p.Id,dir};
    }
    return std::nullopt;
  };
  if (auto found = inspect(root)) return found;
  std::error_code ec;
  std::vector<fs::path> children;
  for (fs::directory_iterator it(root,ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code typeEc;
    if (it->is_directory(typeEc)) children.push_back(it->path());
  }
  std::sort(children.begin(),children.end());
  // Follow launcher order for a collection of named profile directories.
  for (const auto& p : profiles) for (const auto& dir : children)
    if (FoldName(dir.filename().string()) == p.Id)
      if (auto found = inspect(dir)) return found;
  for (const auto& dir : children) if (auto found = inspect(dir)) return found;
  return std::nullopt;
}

// Host-only VFS relocation: profiles keep their standalone directory layout;
// embedded mounts resolve against the directory selected by the same probe.
inline std::filesystem::path ArchiveRoot;
inline std::filesystem::path ProfileArchiveRoot;
inline std::filesystem::path EngineDataRoot;
inline std::string ResolveArchivePath(const std::string& path) {
  namespace fs = std::filesystem;
  if (ArchiveRoot.empty() || ProfileArchiveRoot.empty()) return path;
  fs::path relative = fs::path(path).lexically_relative(ProfileArchiveRoot);
  if (relative.empty() || *relative.begin() == "..") return path;
  fs::path result = ArchiveRoot;
  for (const auto& component : relative) {
    fs::path next = result/component;
    std::error_code ec;
    if (!fs::exists(next,ec)) {
      const auto folded = FoldName(component.string());
      for (fs::directory_iterator it(result,ec),end; !ec && it != end; it.increment(ec)) {
        if (FoldName(it->path().filename().string()) == folded) { next = it->path(); break; }
      }
    }
    result = next;
  }
  return result.string();
}
}  // namespace Impacto::Host
