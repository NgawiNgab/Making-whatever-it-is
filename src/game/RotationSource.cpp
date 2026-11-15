#include "RotationSource.hpp"

namespace camface::game {

void RotationSource::configure(camfacesdk::BuildData const& build) {
    m_ready = false;
    m_reason = "no verified accessor in build data";

    // The data file must declare a component accessor before we read anything.
    // Look for the offset entry that names a reach path; the entry existing IS the
    // verification record (it is only added after a runtime measurement).
    for (std::size_t i = 0; i < build.offsetCount; ++i) {
        camfacesdk::OffsetEntry const& entry = build.offsets[i];
        if (entry.id == "TurnComponentRotation" && !entry.reachVia.empty()) {
            m_ready = true;
            m_reason = "verified accessor present";
            return;
        }
    }
}

void RotationSource::observeTurnCall(void* self, void* deltaPtr) {
    ++m_turnCalls;
    // Deliberately does not dereference anything. Storing the pointers is safe;
    // reading through them is not, until offsets are verified. The evidence the
    // log needs right now is only "the hook fires, with these two arguments".
    m_self = self;
    m_delta = deltaPtr;
}

Reading RotationSource::read() const {
    Reading reading;
    if (!m_ready) return reading;
    // Unreachable with the current data file. When a verified delta is added,
    // the read lands here and is gated on the same fact that enabled this path.
    return reading;
}

}  // namespace camface::game
