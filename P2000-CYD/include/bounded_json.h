#pragma once
#include <ArduinoJson.h>
#include <cstdlib>
#include <cstddef>

// The WROOM has no PSRAM. Refuse oversized responses without consuming the
// remaining heap needed by WiFi; deserialize directly from the HTTP stream.
class BoundedJsonAllocator : public ArduinoJson::Allocator {
  union Header { size_t size; std::max_align_t alignment; };
  size_t used_ = 0, limit_;
public:
  explicit BoundedJsonAllocator(size_t limit) : limit_(limit) {}
  void* allocate(size_t size) override {
    if (size > limit_ - used_) return nullptr;
    auto h = static_cast<Header*>(std::malloc(sizeof(Header) + size));
    if (!h) return nullptr;
    h->size = size; used_ += size; return h + 1;
  }
  void deallocate(void* p) override {
    if (!p) return;
    auto h = static_cast<Header*>(p) - 1;
    used_ -= h->size; std::free(h);
  }
  void* reallocate(void* p, size_t size) override {
    if (!p) return allocate(size);
    auto h = static_cast<Header*>(p) - 1;
    size_t old = h->size;
    if (size > limit_ - (used_ - old)) return nullptr;
    auto next = static_cast<Header*>(std::realloc(h, sizeof(Header) + size));
    if (!next) return nullptr;
    next->size = size; used_ = used_ - old + size; return next + 1;
  }
};
