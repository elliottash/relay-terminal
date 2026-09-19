// SPDX-License-Identifier: AGPL-3.0-or-later
#include "QueueSubmit.h"

namespace relay::queuesubmit {

Decision decide(const State &state) {
    // The agent alone decides. A turn that is running, or one that just left the queue and has
    // not been reported as running yet, is the only thing an agent prompt waits behind — and it
    // waits behind it as a queued item, never by cancelling or reordering it.
    if (state.agentBusy || state.agentTurnStarting) return Decision::Queue;
    return Decision::StartNow;
}

}
