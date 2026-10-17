#include "workqueue.h"
#include "events.h"

#include "config.h"
#include <cassert>
#include <chrono>
#include <vector>
#include <utility>
#include <atomic>
#include <algorithm>
#include <memory>
#include <exception>

#if IMPACTO_HAVE_THREADS
#include <thread>
#if __SWITCH__
#define __unix__
#endif
#include <concurrentqueue/moodycamel/blockingconcurrentqueue.h>
#if __SWITCH__
#undef __unix__
#endif
#endif

namespace Impacto {
namespace WorkQueue {

struct WorkItem {
  void* Data;
  WorkProc Perform;
  WorkCompletionCallbackProc OnComplete;
  std::exception_ptr Error;
  void Handle();
};

static uint32_t WorkCompletedEventType = 0;

static void InitEventType() {
  if (WorkCompletedEventType != 0) return;
  WorkCompletedEventType = Events::Register();
  assert(WorkCompletedEventType != 0);
}

static void DrainCompletions() {
  SDL_Event event;
  std::exception_ptr error;
  while (Events::PollType(&event, WorkCompletedEventType)) {
    try {
      HandleEvent(&event);
    } catch (...) {
      if (!error) error = std::current_exception();
    }
  }
  WorkCompletedEventType = 0;
  if (error) std::rethrow_exception(error);
}

#if IMPACTO_HAVE_THREADS

static std::vector<std::thread> ThreadPool;
static moodycamel::BlockingConcurrentQueue<WorkItem> QueueItems;
static std::atomic<bool> StopWorkers = false;

static void WorkerThread() {
  while (true) {
    WorkItem item;
    bool hasItem =
        QueueItems.wait_dequeue_timed(item, std::chrono::milliseconds(5));
    if (hasItem) {
      item.Handle();
    } else if (StopWorkers) {
      return;
    }
  }
}

void Init() {
  if (!ThreadPool.empty()) return;
  InitEventType();
  StopWorkers = false;
  const uint32_t numThreads =
      std::min(static_cast<int>(std::thread::hardware_concurrency()), 4);
  for (uint32_t i = 0; i < numThreads; ++i) {
    ThreadPool.emplace_back(std::thread(WorkerThread));
  }
}

void Push(void* data, WorkProc worker,
          WorkCompletionCallbackProc completionCallback) {
  WorkItem item;
  item.Data = data;
  item.Perform = worker;
  item.OnComplete = completionCallback;
  if (ThreadPool.empty())
    item.Handle();
  else
    QueueItems.enqueue(std::move(item));
}
void StopWorkQueue() {
  StopWorkers = true;
  for (auto& thd : ThreadPool) {
    thd.join();
  }
  ThreadPool.clear();
  // Finish completed loaders before their scene resources are destroyed.
  DrainCompletions();
}

#else

// If we don't have threads (i.e. on web), do each item right as it comes in for
// now, no actual queue involved.

void Init() { InitEventType(); }
void Push(void* data, WorkProc worker,
          WorkCompletionCallbackProc completionCallback) {
  WorkItem item;
  item.Data = data;
  item.Perform = worker;
  item.OnComplete = completionCallback;
  item.Handle();
}
void StopWorkQueue() {
  DrainCompletions();
}

#endif

void WorkItem::Handle() {
  try {
    Perform(Data);
  } catch (...) {
    Error = std::current_exception();
  }
  WorkItem* copy = new WorkItem(*this);
  SDL_Event evt{};
  evt.type = WorkCompletedEventType;
  evt.user.data1 = copy;
  Events::Push(&evt);
}

bool HandleEvent(SDL_Event* evt) {
  if (evt->type != WorkCompletedEventType) return false;

  std::unique_ptr<WorkItem> item(static_cast<WorkItem*>(evt->user.data1));
  if (item->Error) std::rethrow_exception(item->Error);
  item->OnComplete(item->Data);
  return true;
}

}  // namespace WorkQueue
}  // namespace Impacto
