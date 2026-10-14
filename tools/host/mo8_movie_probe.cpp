#include "impacto_host.h"
#include "video/videosystem.h"
#include "io/vfs.h"
#include <cstdio>
#include <string>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

// Exercise opening/decoder startup/Stop using the actual class declarations.
// No ticks or GL operations run during these lifecycle checks.

int main(int argc, char** argv) {
  if (argc != 5) return 2;
  setvbuf(stdout, nullptr, _IONBF, 0);
  impacto_host_desc_t desc{};
  desc.struct_size = sizeof desc;
  desc.api_version = IMPACTO_HOST_API_VERSION;
  desc.data_dir_utf8 = argv[1];
  desc.writable_dir_utf8 = argv[2];
  desc.initial_width = 1920;
  desc.initial_height = 1080;
  desc.log_level = 5;
  void* host = nullptr;
  if (impacto_host_create(&desc, &host) != 0) return 1;
  if (impacto_host_open_game(host, argv[3], "mo8", argv[3]) != 0) return 1;
  for (int round = 0; round < 3; ++round) {
    unsigned id = round == 1 ? 16 : std::stoul(argv[4]);
    Impacto::Io::Stream* stream = nullptr;
    int err = Impacto::Io::VfsOpen("movie", id, &stream);
    if (err != 0) return 1;
    auto* player = Impacto::Video::Players[0];
    player->Play(stream, false, false);
    std::printf("movie probe round=%d id=%u playing=%d\n", round, id, player->IsPlaying);
    if (!player->IsPlaying) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    player->Stop();
  }
  impacto_host_destroy(host);
  puts("movie probe done");
  return 0;
}
