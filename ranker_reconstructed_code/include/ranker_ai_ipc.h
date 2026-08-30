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
//              "mask":[kAiRlActionCount ints],
//              "tmask":[kAiRlTargetCellCount ints]}      (v8)
//   reply   : {"action":N} or {"action":N,"target":C}    (v8: C = 8x8 grid
//             cell for the spatial actions, -1/absent = none)
//   end     : {"t":"end","reason":"...","frame":F}   (no reply expected)

// Connect to 127.0.0.1:port.  Returns true on success.  Idempotent-safe.
bool AiIpcConnect(unsigned short port);

// True once connected and not yet closed / errored.
bool AiIpcConnected();

// Request the high-level action for one decision.  Blocks for the reply.
// Returns the action index in [0, kAiRlActionCount), or -1 on any I/O/parse
// error (after which AiIpcConnected() reports false).  target_cell (v8, may
// be null) receives the reply's "target" (-1 when absent/invalid); the caller
// applies it only to actions that take one and re-checks the target mask.
int AiIpcRequestAction(unsigned owner, unsigned frame,
    const std::array<float, kAiRlFeatureCount>& features,
    const std::array<std::uint8_t, kAiRlActionCount>& mask,
    const std::array<std::uint8_t, kAiRlTargetCellCount>& target_mask,
    int* target_cell = nullptr);

// Notify the server the match ended, then close.  Safe to call when not
// connected (no-op).
void AiIpcSendEnd(const char* reason, unsigned frame);

// Close the socket / tear down Winsock.  Safe to call multiple times.
void AiIpcClose();

} // namespace ranker

#endif // RANKER_AI_IPC_H
