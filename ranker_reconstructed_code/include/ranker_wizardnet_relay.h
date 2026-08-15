#pragma once

#include "ranker_types.h"

#include <array>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>

namespace ranker {

constexpr u32 kWizardNetRelayJoinRequestOpcode = 0x90;
constexpr u32 kWizardNetRelayLeaveRequestOpcode = 0x91;
constexpr u32 kWizardNetRelayFrameRequestOpcode = 0x92;
constexpr u32 kWizardNetRelayJoinStatusOpcode = 0x93;
constexpr u32 kWizardNetRelayFrameOpcode = 0x94;
constexpr u32 kWizardNetRelayMemberLeftOpcode = 0x95;

constexpr u32 kWizardNetRelayStreamLink = 0;
constexpr u32 kWizardNetRelayStreamMode1 = 1;
constexpr u32 kWizardNetRelayBroadcastMember = 0;
constexpr u32 kWizardNetRelayHostMember = 1;
constexpr u32 kWizardNetRelaySecretBytes = 32;

struct WizardNetRelayState {
    bool enabled = false;
    bool host_mode = false;
    bool relay_secret_available = false;
    u32 game_id = 0;
    u32 local_member_id = 0;
    std::array<u8, kWizardNetRelaySecretBytes> relay_secret{};
    std::array<u32, 8> player_members{};
    std::array<u32, 8> pending_left_game_ids{};
    std::array<u32, 8> pending_left_member_ids{};
    u32 pending_left_count = 0;
};

const WizardNetRelayState& wizardnet_relay_state();
void ResetWizardNetRelayState();
void ConfigureWizardNetRelayState(u32 game_id, u32 local_member_id,
    bool host_mode, const void* relay_secret = nullptr,
    u32 relay_secret_bytes = 0);
bool WizardNetRelayEnabled();
bool WizardNetRelayReadyForGame(u32 game_id);
void ClearWizardNetRelayPlayerMembers();
void SetWizardNetRelayPlayerMember(u32 player_slot, u32 member_id);
u32 WizardNetRelayMemberForPlayer(u32 player_slot);
u32 WizardNetRelayPlayerForMember(u32 member_id);
u32 WizardNetRelayDefaultTargetMember();
SOCKET WizardNetRelaySocketForMember(u32 member_id);
u32 WizardNetRelayMemberForSocket(SOCKET socket);
bool WizardNetRelaySocketIsMember(SOCKET socket);
bool WizardNetRelayCanDiscardStaleAsyncOpcode(u32 opcode);
bool QueueWizardNetRelayJoin(u32 game_id, const void* payload, u32 byte_count);
bool QueueWizardNetRelayFrame(u32 target_member_id, u32 stream_id,
    const void* payload, u32 byte_count);
bool WizardNetRelayPayloadIsEncrypted(const void* payload, u32 byte_count);
bool DecodeWizardNetRelayPayload(u32 game_id, const void* payload,
    u32 byte_count, std::vector<u8>& decoded);
bool FlushWizardNetRelayAsyncSendQueue();
bool QueueWizardNetRelayLeaveForGame(u32 game_id);
bool QueueWizardNetRelayLeave();
void EnqueueWizardNetRelayMode1Payload(const void* payload, u32 byte_count,
    u32 from_member_id);
void RecordWizardNetRelayLinkFrameReceived(u32 game_id, u32 from_member_id,
    u32 byte_count, const char* phase);
bool TakeWizardNetRelayMemberLeft(u32& game_id, u32& member_id);
bool PumpWizardNetRelayMode1Frames();

}

#endif
