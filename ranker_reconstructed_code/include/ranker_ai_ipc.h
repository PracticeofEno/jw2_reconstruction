#ifndef RANKER_AI_IPC_H
#define RANKER_AI_IPC_H

#include "ranker_ai_rl_features.h"

#include <array>

namespace ranker {

// Online policy-in-the-loop IPC (#5b): the deterministic simulation is the
// single controller, but the high-level action each decision comes from an
// off-sim policy (Python) over a localhost TCP socket, per AGENTS.md (the neural
// net runs OUTSIDE the sim; results are published as ordered Mode1 packets).
// Determinism holds on the C++ side conditional on the policy's returns, so a
// deterministic (argmax) policy + fixed -SEED reproduces a match exactly.
//
// Wire format is newline-delimited JSON, one message per line (array sizes
// follow kAiRlFeatureCount / kAiRlActionCount on both ends):
//   request : {"t":"act","owner":O,"frame":F,
//              "feat":[kAiRlFeatureCount floats],
//              "mask":[kAiRlActionCount ints]}
//   reply   : {"action":N}
//   end     : {"t":"end","reason":"...","frame":F}   (no reply expected)

// Connect to 127.0.0.1:port.  Returns true on success.  Idempotent-safe.
bool AiIpcConnect(unsigned short port);

// True once connected and not yet closed / errored.
bool AiIpcConnected();

// Request the high-level action for one decision.  Blocks for the reply.
// Returns the action index in [0, kAiRlActionCount), or -1 on any I/O/parse
// error (after which AiIpcConnected() reports false).
int AiIpcRequestAction(unsigned owner, unsigned frame,
    const std::array<float, kAiRlFeatureCount>& features,
    const std::array<std::uint8_t, kAiRlActionCount>& mask);

// Notify the server the match ended, then close.  Safe to call when not
// connected (no-op).
void AiIpcSendEnd(const char* reason, unsigned frame);

// Close the socket / tear down Winsock.  Safe to call multiple times.
void AiIpcClose();

} // namespace ranker

#endif // RANKER_AI_IPC_H
