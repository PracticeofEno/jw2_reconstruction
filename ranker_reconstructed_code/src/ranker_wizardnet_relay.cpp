#include "ranker_wizardnet_relay.h"

#ifdef _WIN32

#include "ranker_network.h"
#include "ranker_startup_environment.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace ranker {
namespace {

constexpr u32 kLegacyTcpPacketType = 3;
constexpr u32 kLegacyTcpHeaderBytes = 0x0d;
constexpr SOCKET kRelaySocketBase = static_cast<SOCKET>(0x70000000u);
constexpr u32 kRelayMaxPayloadBytes = kLegacyAsyncTcpQueueBytes - kLegacyTcpHeaderBytes - 0x0c;
constexpr u32 kRelayCipherMagic = 0x314c5257u; // "WRL1"
constexpr u32 kRelayCipherHeaderBytes = 28;
constexpr u32 kRelayCipherKeyBytes = kWizardNetRelaySecretBytes;
constexpr u32 kRelayCipherNonceBytes = 12;
constexpr u32 kRelayCipherTagBytes = 8;
constexpr u32 kRelayMaxPlainPayloadBytes =
    kRelayMaxPayloadBytes - kRelayCipherHeaderBytes;

WizardNetRelayState g_wizardnet_relay_state;
std::mutex g_relay_async_tcp_send_mutex;
std::mutex g_relay_log_mutex;
std::atomic<u32> g_relay_sent_link_frame_count{0};
std::atomic<u32> g_relay_sent_mode1_frame_count{0};
std::atomic<u32> g_relay_received_link_frame_count{0};
std::atomic<u32> g_relay_received_mode1_frame_count{0};
std::atomic<u32> g_relay_discarded_async_count{0};
std::atomic<u32> g_relay_drop_count{0};
std::atomic<u32> g_relay_crypto_sequence{0};

struct RelayCryptoKeys {
    std::array<u8, kRelayCipherKeyBytes> cipher{};
    std::array<u8, 16> mac{};
};

enum class RelayCryptoKeySource {
    fallback,
    room_secret,
};

bool should_log_relay_counter(u32 count) {
    return count <= 8 || (count != 0 && (count & (count - 1)) == 0);
}

bool stale_async_opcode_is_safe_to_discard(u32 opcode) {
    switch (opcode) {
    case 0x06:
    case 0x07:
    case 0x09:
    case 0x11:
    case 0x13:
    case 0x15:
    case 0x1a:
    case 0x1c:
    case 0x1e:
    case 0x23:
    case 0x26:
    case 0x27:
    case 0x38:
    case 0x3e:
    case 0x46:
    case 0x64:
    case 0x76:
    case 0x78:
    case 0x7a:
    case 0x7e:
    case 0x80:
    case 0x84:
        return true;
    default:
        return false;
    }
}

void append_relay_log(const char* format, ...) {
    if (format == nullptr) {
        return;
    }

    const std::lock_guard<std::mutex> lock(g_relay_log_mutex);
    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, startup_log_path(), "a") != 0) {
        file = nullptr;
    }
#else
    file = std::fopen(startup_log_path(), "a");
#endif
    if (file == nullptr) {
        return;
    }

    std::fputs("[rebuild] ", file);
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fclose(file);
}

void write_le32(std::vector<u8>& packet, std::size_t offset, u32 value) {
    if (offset + sizeof(value) <= packet.size()) {
        std::memcpy(packet.data() + offset, &value, sizeof(value));
    }
}

void write_le32_raw(u8* data, u32 value) {
    data[0] = static_cast<u8>(value & 0xffu);
    data[1] = static_cast<u8>((value >> 8) & 0xffu);
    data[2] = static_cast<u8>((value >> 16) & 0xffu);
    data[3] = static_cast<u8>((value >> 24) & 0xffu);
}

void write_le64_raw(u8* data, u64 value) {
    for (u32 index = 0; index < 8; ++index) {
        data[index] = static_cast<u8>((value >> (index * 8)) & 0xffu);
    }
}

u32 read_le32_raw(const u8* data) {
    return static_cast<u32>(data[0]) |
        (static_cast<u32>(data[1]) << 8) |
        (static_cast<u32>(data[2]) << 16) |
        (static_cast<u32>(data[3]) << 24);
}

u64 read_le64_raw(const u8* data) {
    u64 value = 0;
    for (u32 index = 0; index < 8; ++index) {
        value |= static_cast<u64>(data[index]) << (index * 8);
    }
    return value;
}

u32 read_le32_checked(const void* data, std::size_t byte_count,
    std::size_t offset) {
    if (data == nullptr || offset + sizeof(u32) > byte_count) {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, static_cast<const u8*>(data) + offset, sizeof(value));
    return value;
}

u32 rotate_left32(u32 value, u32 bits) {
    return (value << bits) | (value >> (32 - bits));
}

u64 rotate_left64(u64 value, u32 bits) {
    return (value << bits) | (value >> (64 - bits));
}

u64 splitmix64_next(u64& state) {
    u64 value = (state += 0x9e3779b97f4a7c15ull);
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

RelayCryptoKeys derive_fallback_relay_crypto_keys(u32 game_id) {
    RelayCryptoKeys keys{};
    u64 seed = 0x8f5f5f76d4a1c3b9ull ^
        (static_cast<u64>(game_id) * 0x100000001b3ull);
    for (u32 index = 0; index < 4; ++index) {
        write_le64_raw(keys.cipher.data() + index * 8,
            splitmix64_next(seed));
    }
    for (u32 index = 0; index < 2; ++index) {
        write_le64_raw(keys.mac.data() + index * 8,
            splitmix64_next(seed));
    }
    return keys;
}

RelayCryptoKeys derive_room_relay_crypto_keys() {
    RelayCryptoKeys keys{};
    keys.cipher = g_wizardnet_relay_state.relay_secret;
    std::memcpy(keys.mac.data(),
        g_wizardnet_relay_state.relay_secret.data() + 16,
        keys.mac.size());
    return keys;
}

RelayCryptoKeys derive_relay_crypto_keys(u32 game_id,
    RelayCryptoKeySource source) {
    if (source == RelayCryptoKeySource::room_secret &&
        g_wizardnet_relay_state.relay_secret_available) {
        return derive_room_relay_crypto_keys();
    }
    return derive_fallback_relay_crypto_keys(game_id);
}

bool relay_crypto_key_source_available(RelayCryptoKeySource source) {
    return source == RelayCryptoKeySource::fallback ||
        g_wizardnet_relay_state.relay_secret_available;
}

const char* relay_crypto_key_source_name(RelayCryptoKeySource source) {
    return source == RelayCryptoKeySource::room_secret ? "room" : "fallback";
}

void make_relay_nonce(u32 game_id, u32 stream_id,
    std::array<u8, kRelayCipherNonceBytes>& nonce) {
    const u32 sequence = g_relay_crypto_sequence.fetch_add(1) + 1;
    const u32 tick = GetTickCount();
    const u32 process_id = GetCurrentProcessId();
    write_le32_raw(nonce.data(), sequence);
    write_le32_raw(nonce.data() + 4,
        tick ^ (stream_id << 24) ^
            (g_wizardnet_relay_state.local_member_id << 8));
    write_le32_raw(nonce.data() + 8, process_id ^ game_id);
}

void chacha20_quarter_round(u32& a, u32& b, u32& c, u32& d) {
    a += b; d ^= a; d = rotate_left32(d, 16);
    c += d; b ^= c; b = rotate_left32(b, 12);
    a += b; d ^= a; d = rotate_left32(d, 8);
    c += d; b ^= c; b = rotate_left32(b, 7);
}

void chacha20_block(const std::array<u8, kRelayCipherKeyBytes>& key,
    const std::array<u8, kRelayCipherNonceBytes>& nonce,
    u32 counter, u8* output) {
    u32 state[16] = {
        0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u,
        read_le32_raw(key.data()),
        read_le32_raw(key.data() + 4),
        read_le32_raw(key.data() + 8),
        read_le32_raw(key.data() + 12),
        read_le32_raw(key.data() + 16),
        read_le32_raw(key.data() + 20),
        read_le32_raw(key.data() + 24),
        read_le32_raw(key.data() + 28),
        counter,
        read_le32_raw(nonce.data()),
        read_le32_raw(nonce.data() + 4),
        read_le32_raw(nonce.data() + 8),
    };
    u32 working[16];
    std::memcpy(working, state, sizeof(working));
    for (u32 round = 0; round < 10; ++round) {
        chacha20_quarter_round(working[0], working[4], working[8], working[12]);
        chacha20_quarter_round(working[1], working[5], working[9], working[13]);
        chacha20_quarter_round(working[2], working[6], working[10], working[14]);
        chacha20_quarter_round(working[3], working[7], working[11], working[15]);
        chacha20_quarter_round(working[0], working[5], working[10], working[15]);
        chacha20_quarter_round(working[1], working[6], working[11], working[12]);
        chacha20_quarter_round(working[2], working[7], working[8], working[13]);
        chacha20_quarter_round(working[3], working[4], working[9], working[14]);
    }
    for (u32 index = 0; index < 16; ++index) {
        write_le32_raw(output + index * 4, working[index] + state[index]);
    }
}

void chacha20_xor(const std::array<u8, kRelayCipherKeyBytes>& key,
    const std::array<u8, kRelayCipherNonceBytes>& nonce,
    u8* data, std::size_t byte_count) {
    u32 counter = 1;
    std::size_t offset = 0;
    while (offset < byte_count) {
        u8 block[64] = {};
        chacha20_block(key, nonce, counter++, block);
        const std::size_t block_bytes =
            byte_count - offset < sizeof(block) ?
                byte_count - offset : sizeof(block);
        for (std::size_t index = 0; index < block_bytes; ++index) {
            data[offset + index] ^= block[index];
        }
        offset += block_bytes;
    }
}

void siphash_round(u64& v0, u64& v1, u64& v2, u64& v3) {
    v0 += v1; v1 = rotate_left64(v1, 13); v1 ^= v0; v0 = rotate_left64(v0, 32);
    v2 += v3; v3 = rotate_left64(v3, 16); v3 ^= v2;
    v0 += v3; v3 = rotate_left64(v3, 21); v3 ^= v0;
    v2 += v1; v1 = rotate_left64(v1, 17); v1 ^= v2; v2 = rotate_left64(v2, 32);
}

u64 siphash24(const std::array<u8, 16>& key, const u8* data,
    std::size_t byte_count) {
    const u64 k0 = read_le64_raw(key.data());
    const u64 k1 = read_le64_raw(key.data() + 8);
    u64 v0 = 0x736f6d6570736575ull ^ k0;
    u64 v1 = 0x646f72616e646f6dull ^ k1;
    u64 v2 = 0x6c7967656e657261ull ^ k0;
    u64 v3 = 0x7465646279746573ull ^ k1;

    const u8* cursor = data;
    std::size_t remaining = byte_count;
    while (remaining >= 8) {
        const u64 message = read_le64_raw(cursor);
        v3 ^= message;
        siphash_round(v0, v1, v2, v3);
        siphash_round(v0, v1, v2, v3);
        v0 ^= message;
        cursor += 8;
        remaining -= 8;
    }

    u64 tail = static_cast<u64>(byte_count) << 56;
    for (std::size_t index = 0; index < remaining; ++index) {
        tail |= static_cast<u64>(cursor[index]) << (index * 8);
    }
    v3 ^= tail;
    siphash_round(v0, v1, v2, v3);
    siphash_round(v0, v1, v2, v3);
    v0 ^= tail;
    v2 ^= 0xff;
    siphash_round(v0, v1, v2, v3);
    siphash_round(v0, v1, v2, v3);
    siphash_round(v0, v1, v2, v3);
    siphash_round(v0, v1, v2, v3);
    return v0 ^ v1 ^ v2 ^ v3;
}

u64 relay_payload_tag(const RelayCryptoKeys& keys, u32 game_id,
    const std::array<u8, kRelayCipherNonceBytes>& nonce,
    const u8* plain, std::size_t plain_bytes) {
    std::vector<u8> tagged(sizeof(u32) * 2, 0);
    write_le32(tagged, 0, game_id);
    write_le32(tagged, 4, static_cast<u32>(plain_bytes));
    tagged.insert(tagged.end(), nonce.begin(), nonce.end());
    if (plain != nullptr && plain_bytes != 0) {
        tagged.insert(tagged.end(), plain, plain + plain_bytes);
    }
    return siphash24(keys.mac, tagged.data(), tagged.size());
}

bool encode_relay_payload(u32 game_id, u32 stream_id, const void* payload,
    u32 byte_count, RelayCryptoKeySource key_source, std::vector<u8>& encoded) {
    encoded.clear();
    if (payload == nullptr || byte_count == 0 ||
        byte_count > kRelayMaxPlainPayloadBytes ||
        !relay_crypto_key_source_available(key_source)) {
        return false;
    }

    const RelayCryptoKeys keys = derive_relay_crypto_keys(game_id, key_source);
    std::array<u8, kRelayCipherNonceBytes> nonce{};
    make_relay_nonce(game_id, stream_id, nonce);
    const u8* plain = static_cast<const u8*>(payload);
    const u64 tag = relay_payload_tag(keys, game_id, nonce, plain, byte_count);

    encoded.resize(kRelayCipherHeaderBytes + byte_count);
    write_le32(encoded, 0, kRelayCipherMagic);
    write_le32(encoded, 4, byte_count);
    std::memcpy(encoded.data() + 8, nonce.data(), nonce.size());
    write_le64_raw(encoded.data() + 20, tag);
    std::memcpy(encoded.data() + kRelayCipherHeaderBytes, plain, byte_count);
    chacha20_xor(keys.cipher, nonce,
        encoded.data() + kRelayCipherHeaderBytes, byte_count);
    return true;
}

bool flush_relay_async_send_queue_unlocked(LegacyAsyncTcpSocket& socket,
    std::size_t required_free_bytes) {
    if (socket.send_length < 0 ||
        socket.send_length > static_cast<i32>(kLegacyAsyncTcpQueueBytes)) {
        return false;
    }

    for (;;) {
        const std::size_t queued_bytes =
            static_cast<std::size_t>(socket.send_length);
        const std::size_t free_bytes = kLegacyAsyncTcpQueueBytes - queued_bytes;
        if ((required_free_bytes != 0 && free_bytes >= required_free_bytes) ||
            socket.send_length == 0) {
            return true;
        }
        if (socket.socket == INVALID_SOCKET) {
            return false;
        }

        i32 sent_count = 0;
        FlushLegacyAsyncTcpSendQueue(socket, &sent_count);
        if (sent_count == SOCKET_ERROR || sent_count <= 0) {
            break;
        }
    }

    if (required_free_bytes == 0) {
        return socket.send_length == 0;
    }
    if (socket.send_length < 0 ||
        socket.send_length > static_cast<i32>(kLegacyAsyncTcpQueueBytes)) {
        return false;
    }
    return required_free_bytes <=
        kLegacyAsyncTcpQueueBytes - static_cast<std::size_t>(socket.send_length);
}

bool queue_server_packet(u32 opcode, const void* payload, u32 byte_count) {
    if (byte_count > kLegacyAsyncTcpQueueBytes - kLegacyTcpHeaderBytes) {
        return false;
    }
    std::vector<u8> packet(kLegacyTcpHeaderBytes + byte_count, 0);
    write_le32(packet, 0, kLegacyTcpPacketType);
    write_le32(packet, 4, opcode);
    write_le32(packet, 8, static_cast<u32>(packet.size()));
    if (payload != nullptr && byte_count != 0) {
        std::memcpy(packet.data() + kLegacyTcpHeaderBytes, payload, byte_count);
    }

    LegacyAsyncTcpSocket& socket = FrontendAsyncTcpSocket0();
    return QueueWizardNetAsyncTcpPacket(socket, packet.data(),
        static_cast<u32>(packet.size()));
}

void queue_member_left(u32 game_id, u32 member_id) {
    if (game_id == 0 || member_id == 0 ||
        game_id != g_wizardnet_relay_state.game_id ||
        g_wizardnet_relay_state.pending_left_count >=
            g_wizardnet_relay_state.pending_left_member_ids.size()) {
        return;
    }
    const u32 index = g_wizardnet_relay_state.pending_left_count++;
    g_wizardnet_relay_state.pending_left_game_ids[index] = game_id;
    g_wizardnet_relay_state.pending_left_member_ids[index] = member_id;
}

}

bool WizardNetRelayCanDiscardStaleAsyncOpcode(u32 opcode) {
    return stale_async_opcode_is_safe_to_discard(opcode);
}

const WizardNetRelayState& wizardnet_relay_state() {
    return g_wizardnet_relay_state;
}

void ResetWizardNetRelayState() {
    const WizardNetRelayState previous = g_wizardnet_relay_state;
    const u32 sent_link_count = g_relay_sent_link_frame_count.exchange(0);
    const u32 sent_mode1_count = g_relay_sent_mode1_frame_count.exchange(0);
    const u32 received_link_count = g_relay_received_link_frame_count.exchange(0);
    const u32 received_mode1_count = g_relay_received_mode1_frame_count.exchange(0);
    const u32 discarded_async_count = g_relay_discarded_async_count.exchange(0);
    const u32 drop_count = g_relay_drop_count.exchange(0);
    if (previous.enabled || previous.game_id != 0 || sent_link_count != 0 ||
        sent_mode1_count != 0 || received_link_count != 0 ||
        received_mode1_count != 0 || discarded_async_count != 0 ||
        drop_count != 0) {
        append_relay_log(
            "wizardnet relay reset game=%lu member=%lu host=%s "
            "sent_link=%lu sent_mode1=%lu recv_link=%lu recv_mode1=%lu "
            "discarded_async=%lu drops=%lu",
            static_cast<unsigned long>(previous.game_id),
            static_cast<unsigned long>(previous.local_member_id),
            previous.host_mode ? "yes" : "no",
            static_cast<unsigned long>(sent_link_count),
            static_cast<unsigned long>(sent_mode1_count),
            static_cast<unsigned long>(received_link_count),
            static_cast<unsigned long>(received_mode1_count),
            static_cast<unsigned long>(discarded_async_count),
            static_cast<unsigned long>(drop_count));
    }
    g_wizardnet_relay_state = {};
}

void ConfigureWizardNetRelayState(u32 game_id, u32 local_member_id,
    bool host_mode, const void* relay_secret, u32 relay_secret_bytes) {
    const auto previous_members = g_wizardnet_relay_state.player_members;
    g_wizardnet_relay_state.enabled = game_id != 0 && local_member_id != 0;
    g_wizardnet_relay_state.game_id = game_id;
    g_wizardnet_relay_state.local_member_id = local_member_id;
    g_wizardnet_relay_state.host_mode = host_mode;
    g_wizardnet_relay_state.relay_secret = {};
    g_wizardnet_relay_state.relay_secret_available =
        relay_secret != nullptr && relay_secret_bytes >= kWizardNetRelaySecretBytes;
    if (g_wizardnet_relay_state.relay_secret_available) {
        std::memcpy(g_wizardnet_relay_state.relay_secret.data(), relay_secret,
            kWizardNetRelaySecretBytes);
    }
    g_wizardnet_relay_state.player_members = previous_members;
    g_wizardnet_relay_state.pending_left_game_ids = {};
    g_wizardnet_relay_state.pending_left_member_ids = {};
    g_wizardnet_relay_state.pending_left_count = 0;
    g_relay_sent_link_frame_count.store(0);
    g_relay_sent_mode1_frame_count.store(0);
    g_relay_received_link_frame_count.store(0);
    g_relay_received_mode1_frame_count.store(0);
    g_relay_discarded_async_count.store(0);
    g_relay_drop_count.store(0);
    append_relay_log(
        "wizardnet relay configured game=%lu member=%lu host=%s crypto_key=%s",
        static_cast<unsigned long>(game_id),
        static_cast<unsigned long>(local_member_id),
        host_mode ? "yes" : "no",
        g_wizardnet_relay_state.relay_secret_available ? "room" : "fallback");
}

bool WizardNetRelayEnabled() {
    return g_wizardnet_relay_state.enabled;
}

bool WizardNetRelayReadyForGame(u32 game_id) {
    return g_wizardnet_relay_state.enabled &&
        g_wizardnet_relay_state.game_id == game_id;
}

void ClearWizardNetRelayPlayerMembers() {
    g_wizardnet_relay_state.player_members = {};
}

void SetWizardNetRelayPlayerMember(u32 player_slot, u32 member_id) {
    if (player_slot >= g_wizardnet_relay_state.player_members.size()) {
        return;
    }
    g_wizardnet_relay_state.player_members[player_slot] = member_id;
}

u32 WizardNetRelayMemberForPlayer(u32 player_slot) {
    if (player_slot >= g_wizardnet_relay_state.player_members.size()) {
        return 0;
    }
    return g_wizardnet_relay_state.player_members[player_slot];
}

u32 WizardNetRelayPlayerForMember(u32 member_id) {
    if (member_id == 0) {
        return 0xffffffffu;
    }
    for (u32 player = 0;
         player < g_wizardnet_relay_state.player_members.size(); ++player) {
        if (g_wizardnet_relay_state.player_members[player] == member_id) {
            return player;
        }
    }
    return 0xffffffffu;
}

u32 WizardNetRelayDefaultTargetMember() {
    if (!g_wizardnet_relay_state.enabled) {
        return 0;
    }
    return g_wizardnet_relay_state.host_mode ?
        kWizardNetRelayBroadcastMember : kWizardNetRelayHostMember;
}

SOCKET WizardNetRelaySocketForMember(u32 member_id) {
    if (member_id == 0 || member_id >= 0x10000u) {
        return INVALID_SOCKET;
    }
    return static_cast<SOCKET>(kRelaySocketBase + member_id);
}

u32 WizardNetRelayMemberForSocket(SOCKET socket) {
    if (!WizardNetRelaySocketIsMember(socket)) {
        return 0;
    }
    return static_cast<u32>(socket - kRelaySocketBase);
}

bool WizardNetRelaySocketIsMember(SOCKET socket) {
    return socket >= kRelaySocketBase && socket < kRelaySocketBase + 0x10000u;
}

bool WizardNetRelayPayloadIsEncrypted(const void* payload, u32 byte_count) {
    return payload != nullptr && byte_count >= kRelayCipherHeaderBytes &&
        read_le32_checked(payload, byte_count, 0) == kRelayCipherMagic;
}

static bool decode_wizardnet_relay_payload(u32 game_id, const void* payload,
    u32 byte_count, bool allow_plaintext,
    bool allow_fallback_after_room_secret, std::vector<u8>& decoded) {
    decoded.clear();
    if (payload == nullptr || byte_count == 0) {
        return false;
    }
    if (!WizardNetRelayPayloadIsEncrypted(payload, byte_count)) {
        if (!allow_plaintext) {
            g_relay_drop_count.fetch_add(1);
            append_relay_log(
                "wizardnet relay plaintext rejected game=%lu bytes=%lu",
                static_cast<unsigned long>(game_id),
                static_cast<unsigned long>(byte_count));
            return false;
        }
        decoded.insert(decoded.end(), static_cast<const u8*>(payload),
            static_cast<const u8*>(payload) + byte_count);
        return true;
    }

    const u8* raw = static_cast<const u8*>(payload);
    const u32 plain_bytes = read_le32_raw(raw + 4);
    if (plain_bytes == 0 ||
        plain_bytes != byte_count - kRelayCipherHeaderBytes ||
        plain_bytes > kRelayMaxPlainPayloadBytes) {
        g_relay_drop_count.fetch_add(1);
        append_relay_log(
            "wizardnet relay crypto rejected game=%lu wire_bytes=%lu plain_bytes=%lu",
            static_cast<unsigned long>(game_id),
            static_cast<unsigned long>(byte_count),
            static_cast<unsigned long>(plain_bytes));
        return false;
    }

    std::array<u8, kRelayCipherNonceBytes> nonce{};
    std::memcpy(nonce.data(), raw + 8, nonce.size());
    const u64 expected_tag = read_le64_raw(raw + 20);
    auto try_decode = [&](RelayCryptoKeySource key_source) -> bool {
        if (!relay_crypto_key_source_available(key_source)) {
            return false;
        }
        decoded.assign(raw + kRelayCipherHeaderBytes,
            raw + kRelayCipherHeaderBytes + plain_bytes);
        const RelayCryptoKeys keys = derive_relay_crypto_keys(game_id, key_source);
        chacha20_xor(keys.cipher, nonce, decoded.data(), decoded.size());
        const u64 actual_tag = relay_payload_tag(keys, game_id, nonce,
            decoded.data(), decoded.size());
        if (actual_tag == expected_tag) {
            return true;
        }
        decoded.clear();
        return false;
    };

    if (try_decode(RelayCryptoKeySource::room_secret) ||
        ((!g_wizardnet_relay_state.relay_secret_available ||
             allow_fallback_after_room_secret) &&
            try_decode(RelayCryptoKeySource::fallback))) {
        return true;
    }

    decoded.clear();
    g_relay_drop_count.fetch_add(1);
    append_relay_log(
        "wizardnet relay crypto auth-failed game=%lu wire_bytes=%lu",
        static_cast<unsigned long>(game_id),
        static_cast<unsigned long>(byte_count));
    return false;
}

bool DecodeWizardNetRelayPayload(u32 game_id, const void* payload,
    u32 byte_count, std::vector<u8>& decoded) {
    return decode_wizardnet_relay_payload(game_id, payload, byte_count,
        true, true, decoded);
}

bool QueueWizardNetRelayJoin(u32 game_id, const void* payload, u32 byte_count) {
    if (game_id == 0 || byte_count > kRelayMaxPlainPayloadBytes) {
        g_relay_drop_count.fetch_add(1);
        append_relay_log(
            "wizardnet relay join rejected game=%lu bytes=%lu max=%lu",
            static_cast<unsigned long>(game_id),
            static_cast<unsigned long>(byte_count),
            static_cast<unsigned long>(kRelayMaxPlainPayloadBytes));
        return false;
    }
    std::vector<u8> encoded;
    if (payload != nullptr && byte_count != 0 &&
        !encode_relay_payload(game_id, kWizardNetRelayStreamLink, payload,
            byte_count, RelayCryptoKeySource::fallback, encoded)) {
        g_relay_drop_count.fetch_add(1);
        append_relay_log(
            "wizardnet relay join crypto rejected game=%lu bytes=%lu",
            static_cast<unsigned long>(game_id),
            static_cast<unsigned long>(byte_count));
        return false;
    }
    std::vector<u8> body(sizeof(u32) + encoded.size(), 0);
    write_le32(body, 0, game_id);
    if (!encoded.empty()) {
        std::memcpy(body.data() + sizeof(u32), encoded.data(), encoded.size());
    }
    const bool queued = queue_server_packet(kWizardNetRelayJoinRequestOpcode,
        body.data(), static_cast<u32>(body.size()));
    append_relay_log(
        "wizardnet relay join %s game=%lu bytes=%lu wire_bytes=%lu crypto=%s crypto_key=%s",
        queued ? "queued" : "blocked",
        static_cast<unsigned long>(game_id),
        static_cast<unsigned long>(byte_count),
        static_cast<unsigned long>(encoded.size()),
        !encoded.empty() ? "yes" : "no",
        !encoded.empty() ? "fallback" : "none");
    if (!queued) {
        g_relay_drop_count.fetch_add(1);
    }
    return queued;
}

bool QueueWizardNetRelayFrame(u32 target_member_id, u32 stream_id,
    const void* payload, u32 byte_count) {
    if (!g_wizardnet_relay_state.enabled || byte_count == 0 ||
        payload == nullptr || byte_count > kRelayMaxPlainPayloadBytes) {
        g_relay_drop_count.fetch_add(1);
        append_relay_log(
            "wizardnet relay frame rejected enabled=%s game=%lu target=%lu "
            "stream=%lu bytes=%lu max=%lu",
            g_wizardnet_relay_state.enabled ? "yes" : "no",
            static_cast<unsigned long>(g_wizardnet_relay_state.game_id),
            static_cast<unsigned long>(target_member_id),
            static_cast<unsigned long>(stream_id),
            static_cast<unsigned long>(byte_count),
            static_cast<unsigned long>(kRelayMaxPlainPayloadBytes));
        return false;
    }
    std::vector<u8> encoded;
    const RelayCryptoKeySource key_source =
        g_wizardnet_relay_state.relay_secret_available ?
            RelayCryptoKeySource::room_secret :
            RelayCryptoKeySource::fallback;
    if (!encode_relay_payload(g_wizardnet_relay_state.game_id, stream_id,
            payload, byte_count, key_source, encoded)) {
        g_relay_drop_count.fetch_add(1);
        append_relay_log(
            "wizardnet relay frame crypto rejected game=%lu target=%lu stream=%lu bytes=%lu",
            static_cast<unsigned long>(g_wizardnet_relay_state.game_id),
            static_cast<unsigned long>(target_member_id),
            static_cast<unsigned long>(stream_id),
            static_cast<unsigned long>(byte_count));
        return false;
    }
    std::vector<u8> body(sizeof(u32) * 3 + encoded.size(), 0);
    write_le32(body, 0, g_wizardnet_relay_state.game_id);
    write_le32(body, 4, target_member_id);
    write_le32(body, 8, stream_id);
    std::memcpy(body.data() + sizeof(u32) * 3, encoded.data(), encoded.size());
    const bool queued = queue_server_packet(kWizardNetRelayFrameRequestOpcode,
        body.data(), static_cast<u32>(body.size()));
    if (!queued) {
        g_relay_drop_count.fetch_add(1);
        append_relay_log(
            "wizardnet relay frame blocked game=%lu target=%lu stream=%lu bytes=%lu wire_bytes=%lu crypto_key=%s",
            static_cast<unsigned long>(g_wizardnet_relay_state.game_id),
            static_cast<unsigned long>(target_member_id),
            static_cast<unsigned long>(stream_id),
            static_cast<unsigned long>(byte_count),
            static_cast<unsigned long>(encoded.size()),
            relay_crypto_key_source_name(key_source));
        return false;
    }

    const u32 counter = stream_id == kWizardNetRelayStreamMode1 ?
        g_relay_sent_mode1_frame_count.fetch_add(1) + 1 :
        g_relay_sent_link_frame_count.fetch_add(1) + 1;
    if (should_log_relay_counter(counter)) {
        append_relay_log(
            "wizardnet relay frame queued game=%lu target=%lu stream=%lu bytes=%lu wire_bytes=%lu crypto=yes crypto_key=%s count=%lu",
            static_cast<unsigned long>(g_wizardnet_relay_state.game_id),
            static_cast<unsigned long>(target_member_id),
            static_cast<unsigned long>(stream_id),
            static_cast<unsigned long>(byte_count),
            static_cast<unsigned long>(encoded.size()),
            relay_crypto_key_source_name(key_source),
            static_cast<unsigned long>(counter));
    }
    return true;
}

bool QueueWizardNetAsyncTcpPacket(LegacyAsyncTcpSocket& socket,
    void* packet, u32 byte_count) {
    if (packet == nullptr || byte_count < kLegacyTcpHeaderBytes ||
        byte_count > kLegacyAsyncTcpQueueBytes) {
        return false;
    }
    const std::lock_guard<std::mutex> lock(g_relay_async_tcp_send_mutex);
    if (!flush_relay_async_send_queue_unlocked(socket, byte_count)) {
        return false;
    }
    PrepareAndQueueLegacyAsyncTcpSend(socket, packet,
        static_cast<i32>(byte_count));
    return true;
}

bool FlushWizardNetRelayAsyncSendQueue() {
    const std::lock_guard<std::mutex> lock(g_relay_async_tcp_send_mutex);
    LegacyAsyncTcpSocket& socket = FrontendAsyncTcpSocket0();
    return flush_relay_async_send_queue_unlocked(socket, 0);
}

bool QueueWizardNetRelayLeaveForGame(u32 game_id) {
    if (game_id == 0) {
        g_relay_drop_count.fetch_add(1);
        append_relay_log("wizardnet relay leave rejected game=0");
        return false;
    }
    const u32 current_game_id = g_wizardnet_relay_state.game_id;
    const u32 current_member_id = g_wizardnet_relay_state.local_member_id;
    if (g_wizardnet_relay_state.game_id == game_id) {
        ResetWizardNetRelayState();
    }
    const bool queued = queue_server_packet(kWizardNetRelayLeaveRequestOpcode, &game_id,
        sizeof(game_id));
    append_relay_log(
        "wizardnet relay leave %s requested=%lu previous_game=%lu previous_member=%lu",
        queued ? "queued" : "blocked",
        static_cast<unsigned long>(game_id),
        static_cast<unsigned long>(current_game_id),
        static_cast<unsigned long>(current_member_id));
    return queued;
}

bool QueueWizardNetRelayLeave() {
    return QueueWizardNetRelayLeaveForGame(g_wizardnet_relay_state.game_id);
}

void EnqueueWizardNetRelayMode1Payload(const void* payload, u32 byte_count,
    u32 from_member_id) {
    const u32 game_id = g_wizardnet_relay_state.game_id;
    if (payload == nullptr || byte_count == 0 ||
        byte_count > kLegacyUdpReceiveQueueBytes) {
        g_relay_drop_count.fetch_add(1);
        append_relay_log(
            "wizardnet relay mode1 receive rejected game=%lu member=%lu bytes=%lu queue_max=%lu",
            static_cast<unsigned long>(game_id),
            static_cast<unsigned long>(from_member_id),
            static_cast<unsigned long>(byte_count),
            static_cast<unsigned long>(kLegacyUdpReceiveQueueBytes));
        return;
    }
    auto& network = legacy_network_state();
    if (byte_count > kLegacyUdpReceiveQueueBytes - network.udp_receive_queue.size()) {
        g_relay_drop_count.fetch_add(1);
        append_relay_log(
            "wizardnet relay mode1 receive dropped game=%lu member=%lu bytes=%lu queued=%zu max=%lu",
            static_cast<unsigned long>(game_id),
            static_cast<unsigned long>(from_member_id),
            static_cast<unsigned long>(byte_count),
            network.udp_receive_queue.size(),
            static_cast<unsigned long>(kLegacyUdpReceiveQueueBytes));
        return;
    }
    network.udp_receive_queue.insert(network.udp_receive_queue.end(),
        static_cast<const u8*>(payload), static_cast<const u8*>(payload) + byte_count);
    network.udp_last_sender = {};
    network.udp_last_sender.sin_family = AF_INET;
    network.udp_last_sender.sin_addr.s_addr = htonl(0x0a640000u | (from_member_id & 0xffu));
    network.udp_last_sender.sin_port = htons(static_cast<u16>(0x7000u + from_member_id));
    const u32 received_count =
        g_relay_received_mode1_frame_count.fetch_add(1) + 1;
    if (should_log_relay_counter(received_count)) {
        append_relay_log(
            "wizardnet relay mode1 received game=%lu member=%lu bytes=%lu queued=%zu count=%lu",
            static_cast<unsigned long>(game_id),
            static_cast<unsigned long>(from_member_id),
            static_cast<unsigned long>(byte_count),
            network.udp_receive_queue.size(),
            static_cast<unsigned long>(received_count));
    }
}

void RecordWizardNetRelayLinkFrameReceived(u32 game_id, u32 from_member_id,
    u32 byte_count, const char* phase) {
    if (game_id == 0 || game_id != g_wizardnet_relay_state.game_id ||
        from_member_id == 0 || byte_count == 0) {
        return;
    }
    const u32 received_count =
        g_relay_received_link_frame_count.fetch_add(1) + 1;
    if (should_log_relay_counter(received_count)) {
        append_relay_log(
            "wizardnet relay link received phase=%s game=%lu member=%lu "
            "bytes=%lu count=%lu",
            phase != nullptr ? phase : "unknown",
            static_cast<unsigned long>(game_id),
            static_cast<unsigned long>(from_member_id),
            static_cast<unsigned long>(byte_count),
            static_cast<unsigned long>(received_count));
    }
}

bool TakeWizardNetRelayMemberLeft(u32& game_id, u32& member_id) {
    if (g_wizardnet_relay_state.pending_left_count == 0) {
        game_id = 0;
        member_id = 0;
        return false;
    }

    game_id = g_wizardnet_relay_state.pending_left_game_ids[0];
    member_id = g_wizardnet_relay_state.pending_left_member_ids[0];
    for (u32 index = 1; index < g_wizardnet_relay_state.pending_left_count; ++index) {
        g_wizardnet_relay_state.pending_left_game_ids[index - 1] =
            g_wizardnet_relay_state.pending_left_game_ids[index];
        g_wizardnet_relay_state.pending_left_member_ids[index - 1] =
            g_wizardnet_relay_state.pending_left_member_ids[index];
    }
    --g_wizardnet_relay_state.pending_left_count;
    const u32 tail = g_wizardnet_relay_state.pending_left_count;
    g_wizardnet_relay_state.pending_left_game_ids[tail] = 0;
    g_wizardnet_relay_state.pending_left_member_ids[tail] = 0;
    return true;
}

bool PumpWizardNetRelayMode1Frames() {
    if (!g_wizardnet_relay_state.enabled) {
        return false;
    }

    LegacyAsyncTcpSocket& socket = FrontendAsyncTcpSocket0();
    ReceiveLegacyAsyncTcpQueue(socket);

    bool consumed = false;
    const u8* payload = GetLegacyAsyncTcpReceiveBuffer(socket);
    i32 byte_count = GetLegacyAsyncTcpReceiveLength(socket);
    while (payload != nullptr && byte_count >= static_cast<i32>(kLegacyTcpHeaderBytes)) {
        const auto available = static_cast<std::size_t>(byte_count);
        const u32 packet_type = read_le32_checked(payload, available, 0);
        const u32 opcode = read_le32_checked(payload, available, 4);
        const u32 packet_bytes = read_le32_checked(payload, available, 8);
        if (packet_type != kLegacyTcpPacketType || packet_bytes < kLegacyTcpHeaderBytes ||
            packet_bytes > available) {
            break;
        }

        if (opcode == kWizardNetRelayFrameOpcode) {
            if (packet_bytes < 0x19) {
                ConsumeLegacyAsyncTcpReceiveQueue(socket, static_cast<i32>(packet_bytes));
                consumed = true;
            } else {
                const u32 game_id = read_le32_checked(payload, packet_bytes, 0x0d);
                const u32 member_id = read_le32_checked(payload, packet_bytes, 0x11);
                const u32 stream_id = read_le32_checked(payload, packet_bytes, 0x15);
                if (game_id == g_wizardnet_relay_state.game_id &&
                    stream_id == kWizardNetRelayStreamMode1) {
                    std::vector<u8> decoded;
                    if (decode_wizardnet_relay_payload(game_id, payload + 0x19,
                            packet_bytes - 0x19, false, false, decoded)) {
                        EnqueueWizardNetRelayMode1Payload(decoded.data(),
                            static_cast<u32>(decoded.size()), member_id);
                    }
                } else if (game_id == g_wizardnet_relay_state.game_id &&
                    stream_id == kWizardNetRelayStreamLink) {
                    std::vector<u8> decoded;
                    if (decode_wizardnet_relay_payload(game_id, payload + 0x19,
                            packet_bytes - 0x19, false, true, decoded)) {
                        RecordWizardNetRelayLinkFrameReceived(game_id, member_id,
                            static_cast<u32>(decoded.size()), "gameplay-stale");
                    }
                } else {
                    g_relay_drop_count.fetch_add(1);
                    append_relay_log(
                        "wizardnet relay frame ignored active_game=%lu packet_game=%lu "
                        "member=%lu stream=%lu bytes=%lu",
                        static_cast<unsigned long>(
                            g_wizardnet_relay_state.game_id),
                        static_cast<unsigned long>(game_id),
                        static_cast<unsigned long>(member_id),
                        static_cast<unsigned long>(stream_id),
                        static_cast<unsigned long>(packet_bytes - 0x19));
                }
                ConsumeLegacyAsyncTcpReceiveQueue(socket, static_cast<i32>(packet_bytes));
                consumed = true;
            }
        } else if (opcode == kWizardNetRelayMemberLeftOpcode) {
            if (packet_bytes >= 0x15) {
                const u32 game_id = read_le32_checked(payload, packet_bytes, 0x0d);
                const u32 member_id = read_le32_checked(payload, packet_bytes, 0x11);
                queue_member_left(game_id, member_id);
                append_relay_log("wizardnet relay member-left received game=%lu member=%lu",
                    static_cast<unsigned long>(game_id),
                    static_cast<unsigned long>(member_id));
            }
            ConsumeLegacyAsyncTcpReceiveQueue(socket, static_cast<i32>(packet_bytes));
            consumed = true;
        } else if (opcode == kWizardNetRelayJoinStatusOpcode) {
            ConsumeLegacyAsyncTcpReceiveQueue(socket, static_cast<i32>(packet_bytes));
            consumed = true;
        } else if (WizardNetRelayCanDiscardStaleAsyncOpcode(opcode)) {
            const u32 discarded_count =
                g_relay_discarded_async_count.fetch_add(1) + 1;
            if (should_log_relay_counter(discarded_count)) {
                append_relay_log(
                    "wizardnet relay stale async packet discarded opcode=0x%02lx "
                    "bytes=%lu count=%lu",
                    static_cast<unsigned long>(opcode),
                    static_cast<unsigned long>(packet_bytes),
                    static_cast<unsigned long>(discarded_count));
            }
            ConsumeLegacyAsyncTcpReceiveQueue(socket, static_cast<i32>(packet_bytes));
            consumed = true;
        } else {
            break;
        }

        payload = GetLegacyAsyncTcpReceiveBuffer(socket);
        byte_count = GetLegacyAsyncTcpReceiveLength(socket);
    }
    return consumed;
}

}

#endif
