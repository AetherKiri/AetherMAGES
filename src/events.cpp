#include "events.h"

#ifdef IMPACTO_HOST_GPU
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

namespace Impacto::Events {
namespace {
struct QueuedEvent {
  SDL_Event Event;
  std::string Text;
};
std::mutex Mutex;
std::deque<QueuedEvent> Queue;
std::string PolledText;
Uint32 NextType = SDL_EVENT_USER;

void Take(SDL_Event* event, QueuedEvent&& queued) {
  *event = queued.Event;
  if (event->type == SDL_EVENT_TEXT_INPUT) {
    PolledText = std::move(queued.Text);
    event->text.text = PolledText.c_str();
  }
}
}

bool Push(const SDL_Event* event) {
  std::lock_guard lock(Mutex);
  QueuedEvent queued{*event, {}};
  if (event->type == SDL_EVENT_TEXT_INPUT && event->text.text)
    queued.Text = event->text.text;
  if (!queued.Event.common.timestamp) {
    queued.Event.common.timestamp = static_cast<Uint64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
  }
  Queue.push_back(std::move(queued));
  return true;
}

bool Poll(SDL_Event* event) {
  std::lock_guard lock(Mutex);
  if (Queue.empty()) return false;
  Take(event, std::move(Queue.front()));
  Queue.pop_front();
  return true;
}

bool PollType(SDL_Event* event, Uint32 type) {
  std::lock_guard lock(Mutex);
  for (auto it = Queue.begin(); it != Queue.end(); ++it) {
    if (it->Event.type != type) continue;
    Take(event, std::move(*it));
    Queue.erase(it);
    return true;
  }
  return false;
}

Uint32 Register() {
  std::lock_guard lock(Mutex);
  return NextType++;
}

void Reset() {
  std::lock_guard lock(Mutex);
  Queue.clear();
  PolledText.clear();
  NextType = SDL_EVENT_USER;
}
}  // namespace Impacto::Events
#else
namespace Impacto::Events {
bool Push(const SDL_Event* event) {
  auto copy = *event;
  return SDL_PushEvent(&copy);
}
bool Poll(SDL_Event* event) { return SDL_PollEvent(event); }
bool PollType(SDL_Event* event, Uint32 type) {
  return SDL_PeepEvents(event, 1, SDL_GETEVENT, type, type) == 1;
}
Uint32 Register() { return SDL_RegisterEvents(1); }
void Reset() {}
}  // namespace Impacto::Events
#endif
