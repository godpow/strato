#include "trace.h"

PERFETTO_TRACK_EVENT_STATIC_STORAGE(); //!< Expands into a structure with static storage for all track events

namespace perfetto {
namespace internal {

void WriteTrackEventArgs(perfetto::EventContext& ctx, const char* name) {
    ctx.event()->set_name(name);
}

}  // namespace internal
}  // namespace perfetto
