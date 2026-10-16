#include "vfs.h"
#include "profile_internal.h"
#include "../io/vfs.h"
#ifdef IMPACTO_HOST_GPU
#include "../host/gameroot.h"
#endif

namespace Impacto {
namespace Profile {
namespace Vfs {
void Configure() {
  EnsurePushMemberOfType("Vfs", LUA_TTABLE);

  {
    EnsurePushMemberOfType("Mounts", LUA_TTABLE);
    ForEachProfileTable<std::string>([](std::string const& name) {
      ForEachProfileArray([&name](size_t index) {
        std::string file(EnsureGetArrayElement<std::string>());
#ifdef IMPACTO_HOST_GPU
        file = Host::ResolveArchivePath(file);
#endif
        Io::VfsMount(name, file);
      });
    });
    Pop();
  }

  Pop();
}
}  // namespace Vfs
}  // namespace Profile
}  // namespace Impacto
