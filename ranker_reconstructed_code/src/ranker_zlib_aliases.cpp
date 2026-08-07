// Compatibility entry points for the statically linked zlib 1.1.3 code.
// These names mirror the recovered executable while delegating to typed zlib
// structures and the bundled implementation.

#include "ranker_types.h"
#include "ranker_trc.h"
#include "ranker_win32_compat.h"

extern "C" {
#include "zutil.h"
#include "deflate.h"
#include "inftrees.h"
#include "infblock.h"
#include "infcodes.h"
#include "inffast.h"
extern int inflate_flush OF((inflate_blocks_statef *, z_streamp, int));
}
#ifdef local
#undef local
#endif

struct static_tree_desc_s {
    const ct_data* static_tree;
    const intf* extra_bits;
    int extra_base;
    int elems;
    int max_length;
};

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

namespace ranker {

const char* GetZlibVersion113() {
    return "1.1.3";
}

const char* GetZlibErrorMessage113(int code) {
    switch (code) {
    case 0:
        return "";
    case 1:
        return "stream end";
    case 2:
        return "need dictionary";
    case -1:
        return "file error";
    case -2:
        return "stream error";
    case -3:
        return "data error";
    case -4:
        return "insufficient memory";
    case -5:
        return "buffer error";
    case -6:
        return "incompatible version";
    default:
        return "unknown zlib error";
    }
}

void* AllocateZlibMemoryWithCalloc113(void*, u32 items, u32 item_size) {
    return std::calloc(items, item_size);
}

void FreeZlibMemory113(void*, void* address) {
    std::free(address);
}

u32 ZlibAdler32_113(u32 adler, const u8* data, u32 byte_count) {
    constexpr u32 kAdlerMod = 65521;
    if (data == nullptr) {
        return 1;
    }

    u32 low = adler & 0xffffu;
    u32 high = (adler >> 16) & 0xffffu;
    for (u32 index = 0; index < byte_count; ++index) {
        low = (low + data[index]) % kAdlerMod;
        high = (high + low) % kAdlerMod;
    }
    return (high << 16) | low;
}

int HandleZlibCompress113MinusOneLevel(void* destination, u32* destination_len,
    const void* source, u32 source_len) {
    return ZlibCompress113WithLevel(destination, destination_len, source, source_len,
        -1);
}

#ifdef _WIN32
HMODULE ZlibRuntime113Module() {
    static HMODULE module = []() -> HMODULE {
        for (const char* name : {"zlib1.dll", "libz.dll", "zlib.dll"}) {
            if (HMODULE loaded = LoadLibraryA(name)) {
                return loaded;
            }
        }
        return nullptr;
    }();
    return module;
}

template <typename Proc>
Proc ZlibRuntime113Proc(const char* name) {
    HMODULE module = ZlibRuntime113Module();
    return module != nullptr
        ? reinterpret_cast<Proc>(GetProcAddress(module, name))
        : nullptr;
}
#endif

constexpr int kZlib113StreamSizeCompat = sizeof(z_stream);

int ZlibInflateReset113(void* stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflateReset")) {
        return proc(stream);
    }
#endif
    return ::inflateReset(static_cast<z_streamp>(stream));
}

int ZlibInflateEnd113(void* stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflateEnd")) {
        return proc(stream);
    }
#endif
    return ::inflateEnd(static_cast<z_streamp>(stream));
}

int ZlibInflateInit2_113(void* stream, int window_bits) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, int, const char*, int);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflateInit2_")) {
        return proc(stream, window_bits, GetZlibVersion113(),
            kZlib113StreamSizeCompat);
    }
#endif
    return ::inflateInit2_(static_cast<z_streamp>(stream), window_bits,
        GetZlibVersion113(), kZlib113StreamSizeCompat);
}

int ZlibInflateInit_113(void* stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, const char*, int);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflateInit_")) {
        return proc(stream, GetZlibVersion113(), kZlib113StreamSizeCompat);
    }
#endif
    return ::inflateInit_(static_cast<z_streamp>(stream),
        GetZlibVersion113(), kZlib113StreamSizeCompat);
}

int ZlibInflate113(void* stream, int flush) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, int);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflate")) {
        return proc(stream, flush);
    }
#endif
    return ::inflate(static_cast<z_streamp>(stream), flush);
}

int ZlibInflateSetDictionary113(void* stream, const u8* dictionary,
    u32 dictionary_len) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, const u8*, u32);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflateSetDictionary")) {
        return proc(stream, dictionary, dictionary_len);
    }
#endif
    return ::inflateSetDictionary(static_cast<z_streamp>(stream),
        reinterpret_cast<const Bytef*>(dictionary),
        static_cast<uInt>(dictionary_len));
}

int ZlibInflateSync113(void* stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflateSync")) {
        return proc(stream);
    }
#endif
    return ::inflateSync(static_cast<z_streamp>(stream));
}

int ZlibInflateSyncPoint113(void* stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*);
    if (Proc proc = ZlibRuntime113Proc<Proc>("inflateSyncPoint")) {
        return proc(stream);
    }
#endif
    return ::inflateSyncPoint(static_cast<z_streamp>(stream));
}

int ZlibDeflateInit_113(void* stream, int level) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, int, const char*, int);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflateInit_")) {
        return proc(stream, level, GetZlibVersion113(),
            kZlib113StreamSizeCompat);
    }
#endif
    return ::deflateInit_(static_cast<z_streamp>(stream), level,
        GetZlibVersion113(), kZlib113StreamSizeCompat);
}

int ZlibDeflateInit2_113(void* stream, int level, int method, int window_bits,
    int mem_level, int strategy) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, int, int, int, int, int, const char*, int);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflateInit2_")) {
        return proc(stream, level, method, window_bits, mem_level, strategy,
            GetZlibVersion113(), kZlib113StreamSizeCompat);
    }
#endif
    return ::deflateInit2_(static_cast<z_streamp>(stream), level, method,
        window_bits, mem_level, strategy, GetZlibVersion113(),
        kZlib113StreamSizeCompat);
}

int ZlibDeflateSetDictionary113(void* stream, const u8* dictionary,
    u32 dictionary_len) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, const u8*, u32);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflateSetDictionary")) {
        return proc(stream, dictionary, dictionary_len);
    }
#endif
    return ::deflateSetDictionary(static_cast<z_streamp>(stream),
        reinterpret_cast<const Bytef*>(dictionary),
        static_cast<uInt>(dictionary_len));
}

int ZlibDeflateReset113(void* stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflateReset")) {
        return proc(stream);
    }
#endif
    return ::deflateReset(static_cast<z_streamp>(stream));
}

int ZlibDeflateParams113(void* stream, int level, int strategy) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, int, int);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflateParams")) {
        return proc(stream, level, strategy);
    }
#endif
    return ::deflateParams(static_cast<z_streamp>(stream), level, strategy);
}

int ZlibDeflate113(void* stream, int flush) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, int);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflate")) {
        return proc(stream, flush);
    }
#endif
    return ::deflate(static_cast<z_streamp>(stream), flush);
}

int ZlibDeflateEnd113(void* stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflateEnd")) {
        return proc(stream);
    }
#endif
    return ::deflateEnd(static_cast<z_streamp>(stream));
}

int ZlibDeflateCopy113(void* destination_stream, void* source_stream) {
#ifdef _WIN32
    using Proc = int(__cdecl*)(void*, void*);
    if (Proc proc = ZlibRuntime113Proc<Proc>("deflateCopy")) {
        return proc(destination_stream, source_stream);
    }
#endif
    return ::deflateCopy(static_cast<z_streamp>(destination_stream),
        static_cast<z_streamp>(source_stream));
}

constexpr int kZlibEndBlockCode = 256;
constexpr u32 kZlibNil = 0;
constexpr u32 kZlibMinLookahead = MAX_MATCH + MIN_MATCH + 1;
constexpr u32 kZlibTooFar = 4096;
constexpr int kZlibRep3To6 = 16;
constexpr int kZlibRepZero3To10 = 17;
constexpr int kZlibRepZero11To138 = 18;
constexpr int kZlibSmallestHeapIndex = 1;
constexpr int kZlibBitBufferSize = 8 * 2 * sizeof(char);

constexpr std::array<int, LENGTH_CODES> kZlibExtraLengthBits{{
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
}};
constexpr std::array<int, D_CODES> kZlibExtraDistanceBits{{
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
}};
constexpr std::array<u8, BL_CODES> kZlibBitLengthOrder{{
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
}};
constexpr std::array<int, LENGTH_CODES> kZlibBaseLength{{
    0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24,
    28, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 0,
}};
constexpr std::array<int, D_CODES> kZlibBaseDistance{{
    0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128,
    192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096,
    6144, 8192, 12288, 16384, 24576,
}};

enum ZlibDeflateBlockState {
    kZlibNeedMore = 0,
    kZlibBlockDone = 1,
    kZlibFinishStarted = 2,
    kZlibFinishDone = 3,
};

u32 ReadDeflateBuffer(void* stream, void* buffer, u32 size);
u32 ReverseDeflateBits(u32 value, u32 bit_count);

struct ZlibDeflateLevelConfig {
    u16 good_length;
    u16 max_lazy;
    u16 nice_length;
    u16 max_chain;
};

constexpr std::array<ZlibDeflateLevelConfig, 10> kZlibDeflateLevelConfigs = {{
    {0, 0, 0, 0},
    {4, 4, 8, 4},
    {4, 5, 16, 8},
    {4, 6, 32, 32},
    {4, 4, 16, 16},
    {8, 16, 32, 32},
    {8, 16, 128, 128},
    {8, 32, 128, 256},
    {32, 128, 258, 1024},
    {32, 258, 258, 4096},
}};

void WriteDeflatePendingWordMsbFirst(void* state, u32 value) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr || deflate->pending_buf == nullptr) {
        return;
    }
    deflate->pending_buf[deflate->pending++] = static_cast<Byte>(value >> 8);
    deflate->pending_buf[deflate->pending++] = static_cast<Byte>(value);
}
void FlushDeflatePendingOutput(void* stream) {
    auto* zstream = static_cast<z_streamp>(stream);
    if (zstream == nullptr || zstream->state == nullptr) {
        return;
    }
    auto* deflate = reinterpret_cast<deflate_state*>(zstream->state);
    unsigned len = static_cast<unsigned>(deflate->pending);
    if (len > zstream->avail_out) {
        len = zstream->avail_out;
    }
    if (len == 0) {
        return;
    }
    std::memcpy(zstream->next_out, deflate->pending_out, len);
    zstream->next_out += len;
    deflate->pending_out += len;
    zstream->total_out += len;
    zstream->avail_out -= len;
    deflate->pending -= static_cast<int>(len);
    if (deflate->pending == 0) {
        deflate->pending_out = deflate->pending_buf;
    }
}
void InitializeDeflateLongestMatchState(void* state) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr) {
        return;
    }
    deflate->window_size = static_cast<ulg>(2U) * deflate->w_size;
    if (deflate->head != nullptr && deflate->hash_size != 0) {
        deflate->head[deflate->hash_size - 1] = 0;
        std::memset(deflate->head, 0,
            (deflate->hash_size - 1) * sizeof(*deflate->head));
    }
    const int level = std::clamp(deflate->level, 0, 9);
    const ZlibDeflateLevelConfig& config =
        kZlibDeflateLevelConfigs[static_cast<std::size_t>(level)];
    deflate->max_lazy_match = config.max_lazy;
    deflate->good_match = config.good_length;
    deflate->nice_match = config.nice_length;
    deflate->max_chain_length = config.max_chain;
    deflate->strstart = 0;
    deflate->block_start = 0;
    deflate->lookahead = 0;
    deflate->match_length = MIN_MATCH - 1;
    deflate->prev_length = MIN_MATCH - 1;
    deflate->match_available = 0;
    deflate->ins_h = 0;
}

void UpdateDeflateHashCompat(deflate_state* deflate, u32& hash, u32 value) {
    hash = ((hash << deflate->hash_shift) ^ value) & deflate->hash_mask;
}

void InsertDeflateStringCompat(deflate_state* deflate, u32 strstart,
    IPos& match_head) {
    UpdateDeflateHashCompat(deflate, deflate->ins_h,
        deflate->window[strstart + (MIN_MATCH - 1)]);
    match_head = deflate->head[deflate->ins_h];
    deflate->prev[strstart & deflate->w_mask] = static_cast<Pos>(match_head);
    deflate->head[deflate->ins_h] = static_cast<Pos>(strstart);
}

void SeedDeflateHashAtCurrentStart(deflate_state* deflate) {
    if (deflate->lookahead >= MIN_MATCH) {
        deflate->ins_h = deflate->window[deflate->strstart];
        UpdateDeflateHashCompat(deflate, deflate->ins_h,
            deflate->window[deflate->strstart + 1]);
    }
}

u32 LongestDeflateMatchCompat(deflate_state* deflate, IPos cur_match) {
    if (deflate == nullptr || deflate->window == nullptr ||
        deflate->prev == nullptr || cur_match == kZlibNil) {
        return 0;
    }

    unsigned chain_length = deflate->max_chain_length;
    int best_len = static_cast<int>(deflate->prev_length);
    int nice_match = deflate->nice_match;
    const IPos limit = deflate->strstart > static_cast<IPos>(MAX_DIST(deflate))
        ? deflate->strstart - static_cast<IPos>(MAX_DIST(deflate))
        : kZlibNil;
    const u32 max_scan = std::min<u32>(MAX_MATCH, deflate->lookahead);
    if (max_scan == 0) {
        return 0;
    }
    if (best_len >= static_cast<int>(max_scan)) {
        return max_scan;
    }
    if (deflate->prev_length >= deflate->good_match) {
        chain_length >>= 2;
    }
    if (nice_match > static_cast<int>(deflate->lookahead)) {
        nice_match = static_cast<int>(deflate->lookahead);
    }

    const Bytef* scan = deflate->window + deflate->strstart;
    do {
        if (cur_match >= deflate->strstart) {
            break;
        }
        const Bytef* match = deflate->window + cur_match;
        if (best_len >= 1 &&
            match[best_len - 1] != scan[best_len - 1]) {
            cur_match = deflate->prev[cur_match & deflate->w_mask];
            continue;
        }
        if (best_len < static_cast<int>(max_scan) &&
            match[best_len] != scan[best_len]) {
            cur_match = deflate->prev[cur_match & deflate->w_mask];
            continue;
        }
        if (match[0] != scan[0] || match[1] != scan[1]) {
            cur_match = deflate->prev[cur_match & deflate->w_mask];
            continue;
        }

        u32 len = 2;
        while (len < max_scan && scan[len] == match[len]) {
            ++len;
        }
        if (len > static_cast<u32>(best_len)) {
            deflate->match_start = cur_match;
            best_len = static_cast<int>(len);
            if (best_len >= nice_match) {
                break;
            }
        }
        cur_match = deflate->prev[cur_match & deflate->w_mask];
    } while (cur_match > limit && --chain_length != 0);

    return std::min<u32>(static_cast<u32>(best_len), deflate->lookahead);
}

void FillDeflateWindowCompat(deflate_state* deflate) {
    if (deflate == nullptr || deflate->strm == nullptr ||
        deflate->window == nullptr) {
        return;
    }

    const uInt wsize = deflate->w_size;
    do {
        unsigned more = static_cast<unsigned>(
            deflate->window_size - static_cast<ulg>(deflate->lookahead) -
            static_cast<ulg>(deflate->strstart));

        if (more == 0 && deflate->strstart == 0 && deflate->lookahead == 0) {
            more = wsize;
        } else if (more == static_cast<unsigned>(-1)) {
            --more;
        } else if (deflate->strstart >= wsize + MAX_DIST(deflate)) {
            zmemcpy(deflate->window, deflate->window + wsize,
                static_cast<unsigned>(wsize));
            deflate->match_start -= wsize;
            deflate->strstart -= wsize;
            deflate->block_start -= static_cast<long>(wsize);

            u32 count = deflate->hash_size;
            Posf* head = &deflate->head[count];
            do {
                const unsigned value = *--head;
                *head = static_cast<Pos>(value >= wsize ? value - wsize : kZlibNil);
            } while (--count != 0);

            count = wsize;
            Posf* prev = &deflate->prev[count];
            do {
                const unsigned value = *--prev;
                *prev = static_cast<Pos>(value >= wsize ? value - wsize : kZlibNil);
            } while (--count != 0);
            more += wsize;
        }

        if (deflate->strm->avail_in == 0) {
            return;
        }

        const u32 read = ReadDeflateBuffer(deflate->strm,
            deflate->window + deflate->strstart + deflate->lookahead, more);
        deflate->lookahead += read;
        SeedDeflateHashAtCurrentStart(deflate);
    } while (deflate->lookahead < kZlibMinLookahead &&
        deflate->strm->avail_in != 0);
}

void FlushDeflateBlockOnlyCompat(deflate_state* deflate, bool eof) {
    _tr_flush_block(deflate,
        deflate->block_start >= 0
            ? reinterpret_cast<charf*>(
                &deflate->window[static_cast<unsigned>(deflate->block_start)])
            : static_cast<charf*>(Z_NULL),
        static_cast<ulg>(static_cast<long>(deflate->strstart) -
            deflate->block_start),
        eof ? 1 : 0);
    deflate->block_start = deflate->strstart;
    FlushDeflatePendingOutput(deflate->strm);
}

int FlushDeflateBlockCompat(deflate_state* deflate, bool eof) {
    FlushDeflateBlockOnlyCompat(deflate, eof);
    if (deflate->strm != nullptr && deflate->strm->avail_out == 0) {
        return eof ? kZlibFinishStarted : kZlibNeedMore;
    }
    return -1;
}

int DeflateStoredBlockMode(void* state, int flush) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr || deflate->strm == nullptr) {
        return Z_STREAM_ERROR;
    }

    ulg max_block_size = 0xffff;
    if (max_block_size > deflate->pending_buf_size - 5) {
        max_block_size = deflate->pending_buf_size - 5;
    }

    for (;;) {
        if (deflate->lookahead <= 1) {
            FillDeflateWindowCompat(deflate);
            if (deflate->lookahead == 0 && flush == Z_NO_FLUSH) {
                return kZlibNeedMore;
            }
            if (deflate->lookahead == 0) {
                break;
            }
        }

        deflate->strstart += deflate->lookahead;
        deflate->lookahead = 0;

        const ulg max_start = static_cast<ulg>(deflate->block_start) +
            max_block_size;
        if (deflate->strstart == 0 ||
            static_cast<ulg>(deflate->strstart) >= max_start) {
            deflate->lookahead = static_cast<uInt>(
                static_cast<ulg>(deflate->strstart) - max_start);
            deflate->strstart = static_cast<uInt>(max_start);
            const int result = FlushDeflateBlockCompat(deflate, false);
            if (result >= 0) {
                return result;
            }
        }
        if (deflate->strstart - static_cast<uInt>(deflate->block_start) >=
            MAX_DIST(deflate)) {
            const int result = FlushDeflateBlockCompat(deflate, false);
            if (result >= 0) {
                return result;
            }
        }
    }

    const bool finish = flush == Z_FINISH;
    const int result = FlushDeflateBlockCompat(deflate, finish);
    if (result >= 0) {
        return result;
    }
    return finish ? kZlibFinishDone : kZlibBlockDone;
}

void FillDeflateWindow(void* state) {
    FillDeflateWindowCompat(static_cast<deflate_state*>(state));
}
u32 ReadDeflateBuffer(void* stream, void* buffer, u32 size) {
    auto* zstream = static_cast<z_streamp>(stream);
    if (zstream == nullptr || zstream->state == nullptr || buffer == nullptr) {
        return 0;
    }
    auto* deflate = reinterpret_cast<deflate_state*>(zstream->state);
    u32 len = zstream->avail_in;
    if (len > size) {
        len = size;
    }
    if (len == 0) {
        return 0;
    }
    zstream->avail_in -= len;
    if (!deflate->noheader) {
        zstream->adler = adler32(zstream->adler, zstream->next_in, len);
    }
    std::memcpy(buffer, zstream->next_in, len);
    zstream->next_in += len;
    zstream->total_in += len;
    return len;
}
int DeflateFastBlockMode(void* state, int flush) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr || deflate->strm == nullptr) {
        return Z_STREAM_ERROR;
    }

    IPos hash_head = kZlibNil;
    int should_flush = 0;
    for (;;) {
        if (deflate->lookahead < kZlibMinLookahead) {
            FillDeflateWindowCompat(deflate);
            if (deflate->lookahead < kZlibMinLookahead &&
                flush == Z_NO_FLUSH) {
                return kZlibNeedMore;
            }
            if (deflate->lookahead == 0) {
                break;
            }
        }

        if (deflate->lookahead >= MIN_MATCH) {
            InsertDeflateStringCompat(deflate, deflate->strstart, hash_head);
        }

        if (hash_head != kZlibNil &&
            deflate->strstart - hash_head <= MAX_DIST(deflate) &&
            deflate->strategy != Z_HUFFMAN_ONLY) {
            deflate->match_length = LongestDeflateMatchCompat(deflate, hash_head);
        }

        if (deflate->match_length >= MIN_MATCH) {
            _tr_tally_dist(deflate, deflate->strstart - deflate->match_start,
                deflate->match_length - MIN_MATCH, should_flush);
            deflate->lookahead -= deflate->match_length;
            if (deflate->match_length <= deflate->max_insert_length &&
                deflate->lookahead >= MIN_MATCH) {
                --deflate->match_length;
                do {
                    ++deflate->strstart;
                    InsertDeflateStringCompat(deflate, deflate->strstart,
                        hash_head);
                } while (--deflate->match_length != 0);
                ++deflate->strstart;
            } else {
                deflate->strstart += deflate->match_length;
                deflate->match_length = 0;
                SeedDeflateHashAtCurrentStart(deflate);
            }
        } else {
            _tr_tally_lit(deflate, deflate->window[deflate->strstart],
                should_flush);
            --deflate->lookahead;
            ++deflate->strstart;
        }

        if (should_flush != 0) {
            const int result = FlushDeflateBlockCompat(deflate, false);
            if (result >= 0) {
                return result;
            }
        }
    }

    const bool finish = flush == Z_FINISH;
    const int result = FlushDeflateBlockCompat(deflate, finish);
    if (result >= 0) {
        return result;
    }
    return finish ? kZlibFinishDone : kZlibBlockDone;
}

u32 LongestDeflateMatch(void* state, u32 cur_match) {
    return LongestDeflateMatchCompat(static_cast<deflate_state*>(state),
        static_cast<IPos>(cur_match));
}

int DeflateSlowBlockMode(void* state, int flush) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr || deflate->strm == nullptr) {
        return Z_STREAM_ERROR;
    }

    IPos hash_head = kZlibNil;
    int should_flush = 0;
    for (;;) {
        if (deflate->lookahead < kZlibMinLookahead) {
            FillDeflateWindowCompat(deflate);
            if (deflate->lookahead < kZlibMinLookahead &&
                flush == Z_NO_FLUSH) {
                return kZlibNeedMore;
            }
            if (deflate->lookahead == 0) {
                break;
            }
        }

        if (deflate->lookahead >= MIN_MATCH) {
            InsertDeflateStringCompat(deflate, deflate->strstart, hash_head);
        }

        deflate->prev_length = deflate->match_length;
        deflate->prev_match = deflate->match_start;
        deflate->match_length = MIN_MATCH - 1;

        if (hash_head != kZlibNil &&
            deflate->prev_length < deflate->max_lazy_match &&
            deflate->strstart - hash_head <= MAX_DIST(deflate)) {
            if (deflate->strategy != Z_HUFFMAN_ONLY) {
                deflate->match_length =
                    LongestDeflateMatchCompat(deflate, hash_head);
            }
            if (deflate->match_length <= 5 &&
                (deflate->strategy == Z_FILTERED ||
                    (deflate->match_length == MIN_MATCH &&
                        deflate->strstart - deflate->match_start >
                            kZlibTooFar))) {
                deflate->match_length = MIN_MATCH - 1;
            }
        }

        if (deflate->prev_length >= MIN_MATCH &&
            deflate->match_length <= deflate->prev_length) {
            const uInt max_insert =
                deflate->strstart + deflate->lookahead - MIN_MATCH;
            _tr_tally_dist(deflate, deflate->strstart - 1 - deflate->prev_match,
                deflate->prev_length - MIN_MATCH, should_flush);
            deflate->lookahead -= deflate->prev_length - 1;
            deflate->prev_length -= 2;
            do {
                if (++deflate->strstart <= max_insert) {
                    InsertDeflateStringCompat(deflate, deflate->strstart,
                        hash_head);
                }
            } while (--deflate->prev_length != 0);
            deflate->match_available = 0;
            deflate->match_length = MIN_MATCH - 1;
            ++deflate->strstart;

            if (should_flush != 0) {
                const int result = FlushDeflateBlockCompat(deflate, false);
                if (result >= 0) {
                    return result;
                }
            }
        } else if (deflate->match_available != 0) {
            _tr_tally_lit(deflate, deflate->window[deflate->strstart - 1],
                should_flush);
            if (should_flush != 0) {
                FlushDeflateBlockOnlyCompat(deflate, false);
            }
            ++deflate->strstart;
            --deflate->lookahead;
            if (deflate->strm->avail_out == 0) {
                return kZlibNeedMore;
            }
        } else {
            deflate->match_available = 1;
            ++deflate->strstart;
            --deflate->lookahead;
        }
    }

    if (deflate->match_available != 0) {
        _tr_tally_lit(deflate, deflate->window[deflate->strstart - 1],
            should_flush);
        deflate->match_available = 0;
    }

    const bool finish = flush == Z_FINISH;
    const int result = FlushDeflateBlockCompat(deflate, finish);
    if (result >= 0) {
        return result;
    }
    return finish ? kZlibFinishDone : kZlibBlockDone;
}
void ResetInflateBlocksState(void* blocks_state, void* stream, u32* check_value) {
    ::inflate_blocks_reset(
        static_cast<inflate_blocks_statef*>(blocks_state),
        static_cast<z_streamp>(stream),
        reinterpret_cast<uLongf*>(check_value));
}
void* CreateInflateBlocksState(void* stream, void* check_function, u32 window_size) {
    return ::inflate_blocks_new(static_cast<z_streamp>(stream),
        reinterpret_cast<check_func>(check_function),
        static_cast<uInt>(window_size));
}
int ProcessInflateBlocks(void* blocks_state, void* stream, int result) {
    return ::inflate_blocks(static_cast<inflate_blocks_statef*>(blocks_state),
        static_cast<z_streamp>(stream), result);
}
void SetInflateBlocksDictionaryWindow(
    void* blocks_state, const u8* dictionary, u32 dictionary_len) {
    ::inflate_set_dictionary(static_cast<inflate_blocks_statef*>(blocks_state),
        reinterpret_cast<const Bytef*>(dictionary),
        static_cast<uInt>(dictionary_len));
}
int CheckInflateBlocksSyncPoint(void* blocks_state) {
    return ::inflate_blocks_sync_point(
        static_cast<inflate_blocks_statef*>(blocks_state));
}
void InitializeDeflateTreeState(void* state) {
    if (state != nullptr) {
        _tr_init(static_cast<deflate_state*>(state));
    }
}
void InitializeStaticDeflateTrees() {}
void ResetDeflateTreeBlockState(void* state) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr) {
        return;
    }
    for (int index = 0; index < L_CODES; ++index) {
        deflate->dyn_ltree[index].Freq = 0;
    }
    for (int index = 0; index < D_CODES; ++index) {
        deflate->dyn_dtree[index].Freq = 0;
    }
    for (int index = 0; index < BL_CODES; ++index) {
        deflate->bl_tree[index].Freq = 0;
    }
    deflate->dyn_ltree[kZlibEndBlockCode].Freq = 1;
    deflate->opt_len = 0;
    deflate->static_len = 0;
    deflate->last_lit = 0;
    deflate->matches = 0;
}
void EmitDeflateStoredBlock(void* state, const u8* buffer, u32 stored_len,
    bool last_block) {
    if (state != nullptr) {
        _tr_stored_block(static_cast<deflate_state*>(state),
            reinterpret_cast<charf*>(const_cast<u8*>(buffer)),
            static_cast<ulg>(stored_len), last_block ? 1 : 0);
    }
}
void EmitDeflateAlignmentBlock(void* state) {
    if (state != nullptr) {
        _tr_align(static_cast<deflate_state*>(state));
    }
}
int FlushDeflateBlock(void* state, const u8* buffer, u32 stored_len,
    bool last_block) {
    if (state == nullptr) {
        return Z_STREAM_ERROR;
    }
    _tr_flush_block(static_cast<deflate_state*>(state),
        reinterpret_cast<charf*>(const_cast<u8*>(buffer)),
        static_cast<ulg>(stored_len), last_block ? 1 : 0);
    return Z_OK;
}
bool DeflateTreeNodeIsSmaller(const ct_data* tree, int lhs, int rhs,
    const uch* depth) {
    return tree[lhs].Freq < tree[rhs].Freq ||
        (tree[lhs].Freq == tree[rhs].Freq && depth[lhs] <= depth[rhs]);
}

void DeflateTreePriorityDownHeapCompat(deflate_state* deflate, ct_data* tree,
    int heap_index) {
    if (deflate == nullptr || tree == nullptr || heap_index <= 0) {
        return;
    }
    const int value = deflate->heap[heap_index];
    int child = heap_index << 1;
    while (child <= deflate->heap_len) {
        if (child < deflate->heap_len &&
            DeflateTreeNodeIsSmaller(tree, deflate->heap[child + 1],
                deflate->heap[child], deflate->depth)) {
            ++child;
        }
        if (DeflateTreeNodeIsSmaller(tree, value, deflate->heap[child],
            deflate->depth)) {
            break;
        }
        deflate->heap[heap_index] = deflate->heap[child];
        heap_index = child;
        child <<= 1;
    }
    deflate->heap[heap_index] = value;
}

void GenerateDeflateBitLengthsCompat(deflate_state* deflate, tree_desc* desc) {
    if (deflate == nullptr || desc == nullptr || desc->dyn_tree == nullptr ||
        desc->stat_desc == nullptr) {
        return;
    }

    ct_data* tree = desc->dyn_tree;
    const int max_code = desc->max_code;
    const ct_data* static_tree = desc->stat_desc->static_tree;
    const intf* extra_bits = desc->stat_desc->extra_bits;
    const int extra_base = desc->stat_desc->extra_base;
    const int max_length = desc->stat_desc->max_length;
    int overflow = 0;

    for (int bits = 0; bits <= MAX_BITS; ++bits) {
        deflate->bl_count[bits] = 0;
    }

    tree[deflate->heap[deflate->heap_max]].Len = 0;
    int heap = deflate->heap_max + 1;
    for (; heap < HEAP_SIZE; ++heap) {
        const int node = deflate->heap[heap];
        int bits = tree[tree[node].Dad].Len + 1;
        if (bits > max_length) {
            bits = max_length;
            ++overflow;
        }
        tree[node].Len = static_cast<ush>(bits);
        if (node > max_code) {
            continue;
        }

        ++deflate->bl_count[bits];
        const int extra = node >= extra_base && extra_bits != nullptr
            ? extra_bits[node - extra_base]
            : 0;
        const ush frequency = tree[node].Freq;
        deflate->opt_len += static_cast<ulg>(frequency) *
            static_cast<ulg>(bits + extra);
        if (static_tree != nullptr) {
            deflate->static_len += static_cast<ulg>(frequency) *
                static_cast<ulg>(static_tree[node].Len + extra);
        }
    }

    if (overflow == 0) {
        return;
    }

    do {
        int bits = max_length - 1;
        while (bits > 0 && deflate->bl_count[bits] == 0) {
            --bits;
        }
        --deflate->bl_count[bits];
        deflate->bl_count[bits + 1] += 2;
        --deflate->bl_count[max_length];
        overflow -= 2;
    } while (overflow > 0);

    for (int bits = max_length; bits != 0; --bits) {
        int count = deflate->bl_count[bits];
        while (count != 0) {
            const int node = deflate->heap[--heap];
            if (node > max_code) {
                continue;
            }
            if (tree[node].Len != static_cast<unsigned>(bits)) {
                deflate->opt_len +=
                    static_cast<long>(bits - tree[node].Len) *
                    static_cast<long>(tree[node].Freq);
                tree[node].Len = static_cast<ush>(bits);
            }
            --count;
        }
    }
}

void GenerateDeflateCodesCompat(ct_data* tree, int max_code, ushf* bl_count) {
    if (tree == nullptr || bl_count == nullptr) {
        return;
    }

    std::array<ush, MAX_BITS + 1> next_code{};
    ush code = 0;
    for (int bits = 1; bits <= MAX_BITS; ++bits) {
        code = static_cast<ush>((code + bl_count[bits - 1]) << 1);
        next_code[bits] = code;
    }
    for (int node = 0; node <= max_code; ++node) {
        const int len = tree[node].Len;
        if (len == 0) {
            continue;
        }
        tree[node].Code = static_cast<ush>(
            ReverseDeflateBits(next_code[len]++, static_cast<u32>(len)));
    }
}

void BuildDeflateTreeCompat(deflate_state* deflate, tree_desc* desc) {
    if (deflate == nullptr || desc == nullptr || desc->dyn_tree == nullptr ||
        desc->stat_desc == nullptr) {
        return;
    }

    ct_data* tree = desc->dyn_tree;
    const ct_data* static_tree = desc->stat_desc->static_tree;
    const int elems = desc->stat_desc->elems;
    int max_code = -1;

    deflate->heap_len = 0;
    deflate->heap_max = HEAP_SIZE;
    for (int node = 0; node < elems; ++node) {
        if (tree[node].Freq != 0) {
            deflate->heap[++deflate->heap_len] = max_code = node;
            deflate->depth[node] = 0;
        } else {
            tree[node].Len = 0;
        }
    }

    while (deflate->heap_len < 2) {
        const int node = deflate->heap[++deflate->heap_len] =
            max_code < 2 ? ++max_code : 0;
        tree[node].Freq = 1;
        deflate->depth[node] = 0;
        --deflate->opt_len;
        if (static_tree != nullptr) {
            deflate->static_len -= static_tree[node].Len;
        }
    }
    desc->max_code = max_code;

    for (int node = deflate->heap_len / 2; node >= 1; --node) {
        DeflateTreePriorityDownHeapCompat(deflate, tree, node);
    }

    int next_node = elems;
    do {
        const int least = deflate->heap[kZlibSmallestHeapIndex];
        deflate->heap[kZlibSmallestHeapIndex] =
            deflate->heap[deflate->heap_len--];
        DeflateTreePriorityDownHeapCompat(deflate, tree,
            kZlibSmallestHeapIndex);
        const int second = deflate->heap[kZlibSmallestHeapIndex];

        deflate->heap[--deflate->heap_max] = least;
        deflate->heap[--deflate->heap_max] = second;

        tree[next_node].Freq = static_cast<ush>(
            tree[least].Freq + tree[second].Freq);
        deflate->depth[next_node] = static_cast<uch>(
            std::max(deflate->depth[least], deflate->depth[second]) + 1);
        tree[least].Dad = tree[second].Dad = static_cast<ush>(next_node);
        deflate->heap[kZlibSmallestHeapIndex] = next_node++;
        DeflateTreePriorityDownHeapCompat(deflate, tree,
            kZlibSmallestHeapIndex);
    } while (deflate->heap_len >= 2);

    deflate->heap[--deflate->heap_max] =
        deflate->heap[kZlibSmallestHeapIndex];
    GenerateDeflateBitLengthsCompat(deflate, desc);
    GenerateDeflateCodesCompat(tree, max_code, deflate->bl_count);
}

void ScanDeflateTreeCompat(deflate_state* deflate, ct_data* tree, int max_code) {
    if (deflate == nullptr || tree == nullptr || max_code < 0) {
        return;
    }

    int previous_length = -1;
    int next_length = tree[0].Len;
    int count = 0;
    int max_count = next_length == 0 ? 138 : 7;
    int min_count = next_length == 0 ? 3 : 4;
    tree[max_code + 1].Len = static_cast<ush>(0xffff);

    for (int node = 0; node <= max_code; ++node) {
        const int current_length = next_length;
        next_length = tree[node + 1].Len;
        if (++count < max_count && current_length == next_length) {
            continue;
        }
        if (count < min_count) {
            deflate->bl_tree[current_length].Freq += static_cast<ush>(count);
        } else if (current_length != 0) {
            if (current_length != previous_length) {
                ++deflate->bl_tree[current_length].Freq;
            }
            ++deflate->bl_tree[kZlibRep3To6].Freq;
        } else if (count <= 10) {
            ++deflate->bl_tree[kZlibRepZero3To10].Freq;
        } else {
            ++deflate->bl_tree[kZlibRepZero11To138].Freq;
        }

        count = 0;
        previous_length = current_length;
        if (next_length == 0) {
            max_count = 138;
            min_count = 3;
        } else if (current_length == next_length) {
            max_count = 6;
            min_count = 3;
        } else {
            max_count = 7;
            min_count = 4;
        }
    }
}

void PutDeflatePendingShortLsbFirst(deflate_state* deflate, ush value) {
    deflate->pending_buf[deflate->pending++] = static_cast<Byte>(value);
    deflate->pending_buf[deflate->pending++] = static_cast<Byte>(value >> 8);
}

void SendDeflateBitsCompat(deflate_state* deflate, int value, int length) {
    if (deflate == nullptr || deflate->pending_buf == nullptr ||
        length <= 0) {
        return;
    }

    if (deflate->bi_valid > kZlibBitBufferSize - length) {
        const int val = value;
        deflate->bi_buf = static_cast<ush>(
            deflate->bi_buf | (val << deflate->bi_valid));
        PutDeflatePendingShortLsbFirst(deflate, deflate->bi_buf);
        deflate->bi_buf = static_cast<ush>(
            static_cast<unsigned>(val) >>
            (kZlibBitBufferSize - deflate->bi_valid));
        deflate->bi_valid += length - kZlibBitBufferSize;
    } else {
        deflate->bi_buf = static_cast<ush>(
            deflate->bi_buf | (value << deflate->bi_valid));
        deflate->bi_valid += length;
    }
}

void SendDeflateCodeCompat(deflate_state* deflate, int code,
    const ct_data* tree) {
    if (tree == nullptr) {
        return;
    }
    SendDeflateBitsCompat(deflate, tree[code].Code, tree[code].Len);
}

void SendDeflateTreeCompat(deflate_state* deflate, ct_data* tree,
    int max_code) {
    if (deflate == nullptr || tree == nullptr || max_code < 0) {
        return;
    }

    int previous_length = -1;
    int next_length = tree[0].Len;
    int count = 0;
    int max_count = next_length == 0 ? 138 : 7;
    int min_count = next_length == 0 ? 3 : 4;

    for (int node = 0; node <= max_code; ++node) {
        const int current_length = next_length;
        next_length = tree[node + 1].Len;
        if (++count < max_count && current_length == next_length) {
            continue;
        }
        if (count < min_count) {
            do {
                SendDeflateCodeCompat(deflate, current_length,
                    deflate->bl_tree);
            } while (--count != 0);
        } else if (current_length != 0) {
            if (current_length != previous_length) {
                SendDeflateCodeCompat(deflate, current_length,
                    deflate->bl_tree);
                --count;
            }
            SendDeflateCodeCompat(deflate, kZlibRep3To6, deflate->bl_tree);
            SendDeflateBitsCompat(deflate, count - 3, 2);
        } else if (count <= 10) {
            SendDeflateCodeCompat(deflate, kZlibRepZero3To10,
                deflate->bl_tree);
            SendDeflateBitsCompat(deflate, count - 3, 3);
        } else {
            SendDeflateCodeCompat(deflate, kZlibRepZero11To138,
                deflate->bl_tree);
            SendDeflateBitsCompat(deflate, count - 11, 7);
        }

        count = 0;
        previous_length = current_length;
        if (next_length == 0) {
            max_count = 138;
            min_count = 3;
        } else if (current_length == next_length) {
            max_count = 6;
            min_count = 3;
        } else {
            max_count = 7;
            min_count = 4;
        }
    }
}

int BuildDeflateBitLengthTreeCompat(deflate_state* deflate) {
    if (deflate == nullptr) {
        return Z_STREAM_ERROR;
    }
    ScanDeflateTreeCompat(deflate, deflate->dyn_ltree, deflate->l_desc.max_code);
    ScanDeflateTreeCompat(deflate, deflate->dyn_dtree, deflate->d_desc.max_code);
    BuildDeflateTreeCompat(deflate, &deflate->bl_desc);

    int max_blindex = BL_CODES - 1;
    for (; max_blindex >= 3; --max_blindex) {
        if (deflate->bl_tree[kZlibBitLengthOrder[max_blindex]].Len != 0) {
            break;
        }
    }
    deflate->opt_len += 3 * (max_blindex + 1) + 5 + 5 + 4;
    return max_blindex;
}

void SendAllDeflateTreesCompat(deflate_state* deflate, int literal_codes,
    int distance_codes, int bit_length_codes) {
    if (deflate == nullptr) {
        return;
    }
    SendDeflateBitsCompat(deflate, literal_codes - 257, 5);
    SendDeflateBitsCompat(deflate, distance_codes - 1, 5);
    SendDeflateBitsCompat(deflate, bit_length_codes - 4, 4);
    for (int rank = 0; rank < bit_length_codes; ++rank) {
        SendDeflateBitsCompat(deflate,
            deflate->bl_tree[kZlibBitLengthOrder[rank]].Len, 3);
    }
    SendDeflateTreeCompat(deflate, deflate->dyn_ltree, literal_codes - 1);
    SendDeflateTreeCompat(deflate, deflate->dyn_dtree, distance_codes - 1);
}

void CompressDeflateBlockCompat(deflate_state* deflate, const ct_data* literal_tree,
    const ct_data* distance_tree) {
    if (deflate == nullptr || literal_tree == nullptr ||
        distance_tree == nullptr) {
        return;
    }

    unsigned index = 0;
    while (index < deflate->last_lit) {
        unsigned distance = deflate->d_buf[index];
        int literal_or_length = deflate->l_buf[index++];
        if (distance == 0) {
            SendDeflateCodeCompat(deflate, literal_or_length, literal_tree);
            continue;
        }

        const unsigned length_code = _length_code[literal_or_length];
        SendDeflateCodeCompat(deflate,
            static_cast<int>(length_code + LITERALS + 1), literal_tree);
        int extra = kZlibExtraLengthBits[length_code];
        if (extra != 0) {
            literal_or_length -= kZlibBaseLength[length_code];
            SendDeflateBitsCompat(deflate, literal_or_length, extra);
        }

        --distance;
        const unsigned distance_code = d_code(distance);
        SendDeflateCodeCompat(deflate, distance_code, distance_tree);
        extra = kZlibExtraDistanceBits[distance_code];
        if (extra != 0) {
            distance -= kZlibBaseDistance[distance_code];
            SendDeflateBitsCompat(deflate, static_cast<int>(distance), extra);
        }
    }

    SendDeflateCodeCompat(deflate, kZlibEndBlockCode, literal_tree);
    deflate->last_eob_len = literal_tree[kZlibEndBlockCode].Len;
}

void BuildDeflateTree(void* state) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate != nullptr) {
        BuildDeflateTreeCompat(deflate, &deflate->l_desc);
    }
}

void BuildDeflateTree(void* state, void* desc) {
    BuildDeflateTreeCompat(static_cast<deflate_state*>(state),
        static_cast<tree_desc*>(desc));
}

void DeflateTreePriorityDownHeap(void* state, void* tree, u32 heap_index) {
    DeflateTreePriorityDownHeapCompat(static_cast<deflate_state*>(state),
        static_cast<ct_data*>(tree), static_cast<int>(heap_index));
}

void GenerateDeflateBitLengths(void* state, void* desc) {
    GenerateDeflateBitLengthsCompat(static_cast<deflate_state*>(state),
        static_cast<tree_desc*>(desc));
}

void GenerateDeflateCodes(void* tree, u32 max_code, void* bl_count) {
    GenerateDeflateCodesCompat(static_cast<ct_data*>(tree),
        static_cast<int>(max_code), static_cast<ushf*>(bl_count));
}

int BuildDeflateBitLengthTree(void* state) {
    return BuildDeflateBitLengthTreeCompat(static_cast<deflate_state*>(state));
}

void ScanDeflateTree(void* state, void* tree, int max_code) {
    ScanDeflateTreeCompat(static_cast<deflate_state*>(state),
        static_cast<ct_data*>(tree), max_code);
}

void SendDeflateTree(void* state, void* tree, int max_code) {
    SendDeflateTreeCompat(static_cast<deflate_state*>(state),
        static_cast<ct_data*>(tree), max_code);
}

void SendAllDeflateTrees(void* state, int literal_codes, int distance_codes,
    int bit_length_codes) {
    SendAllDeflateTreesCompat(static_cast<deflate_state*>(state),
        literal_codes, distance_codes, bit_length_codes);
}
bool TallyDeflateLiteralOrDistance(void* state, u32 distance, u32 literal_or_length) {
    return state != nullptr &&
        _tr_tally(static_cast<deflate_state*>(state), distance,
            literal_or_length) != 0;
}
void CompressDeflateBlock(void* state, void* literal_tree, void* distance_tree) {
    CompressDeflateBlockCompat(static_cast<deflate_state*>(state),
        static_cast<ct_data*>(literal_tree),
        static_cast<ct_data*>(distance_tree));
}
void SetDeflateDataType(void* state) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr) {
        return;
    }
    unsigned ascii_freq = 0;
    unsigned binary_freq = 0;
    int index = 0;
    while (index < 7) {
        binary_freq += deflate->dyn_ltree[index++].Freq;
    }
    while (index < 128) {
        ascii_freq += deflate->dyn_ltree[index++].Freq;
    }
    while (index < LITERALS) {
        binary_freq += deflate->dyn_ltree[index++].Freq;
    }
    deflate->data_type =
        static_cast<Byte>(binary_freq > (ascii_freq >> 2) ? Z_BINARY : Z_ASCII);
}
u32 ReverseDeflateBits(u32 value, u32 bit_count) {
    u32 reversed = 0;
    for (u32 index = 0; index < bit_count; ++index) {
        reversed = (reversed << 1) | (value & 1u);
        value >>= 1;
    }
    return reversed;
}
void FlushDeflateBitBuffer(void* state) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr || deflate->pending_buf == nullptr) {
        return;
    }
    if (deflate->bi_valid == 16) {
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(deflate->bi_buf);
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(deflate->bi_buf >> 8);
        deflate->bi_buf = 0;
        deflate->bi_valid = 0;
    } else if (deflate->bi_valid >= 8) {
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(deflate->bi_buf);
        deflate->bi_buf >>= 8;
        deflate->bi_valid -= 8;
    }
}
void WindUpDeflateBitBuffer(void* state) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr || deflate->pending_buf == nullptr) {
        return;
    }
    if (deflate->bi_valid > 8) {
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(deflate->bi_buf);
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(deflate->bi_buf >> 8);
    } else if (deflate->bi_valid > 0) {
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(deflate->bi_buf);
    }
    deflate->bi_buf = 0;
    deflate->bi_valid = 0;
}
void CopyDeflateStoredBlock(void* state, const u8* buffer, u32 len, bool header) {
    auto* deflate = static_cast<deflate_state*>(state);
    if (deflate == nullptr || deflate->pending_buf == nullptr) {
        return;
    }
    WindUpDeflateBitBuffer(state);
    deflate->last_eob_len = 8;
    if (header) {
        const auto length = static_cast<ush>(len);
        deflate->pending_buf[deflate->pending++] = static_cast<Byte>(length);
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(length >> 8);
        const auto inverse = static_cast<ush>(~length);
        deflate->pending_buf[deflate->pending++] = static_cast<Byte>(inverse);
        deflate->pending_buf[deflate->pending++] =
            static_cast<Byte>(inverse >> 8);
    }
    if (buffer != nullptr && len != 0) {
        std::memcpy(deflate->pending_buf + deflate->pending, buffer, len);
        deflate->pending += static_cast<int>(len);
    }
}
void* CreateInflateCodesState(
    u32 literal_bits, u32 distance_bits, void* literal_tree, void* distance_tree,
    void* stream) {
    return ::inflate_codes_new(static_cast<uInt>(literal_bits),
        static_cast<uInt>(distance_bits),
        static_cast<inflate_huft*>(literal_tree),
        static_cast<inflate_huft*>(distance_tree),
        static_cast<z_streamp>(stream));
}
int ProcessInflateCodes(void* blocks_state, void* stream, int result) {
    return ::inflate_codes(static_cast<inflate_blocks_statef*>(blocks_state),
        static_cast<z_streamp>(stream), result);
}
void FreeInflateCodesState(void* codes_state, void* stream) {
    ::inflate_codes_free(static_cast<inflate_codes_statef*>(codes_state),
        static_cast<z_streamp>(stream));
}
int BuildInflateCodeLengthTree(void* lengths, void* bits, void* table,
    void* hufts, void* stream) {
    return ::inflate_trees_bits(static_cast<uIntf*>(lengths),
        static_cast<uIntf*>(bits),
        reinterpret_cast<inflate_huft**>(table),
        static_cast<inflate_huft*>(hufts),
        static_cast<z_streamp>(stream));
}
int BuildInflateHuffmanTree(void* lengths, u32 code_count, u32 simple_count,
    const void* base_values, const void* extra_bits, void* table_out,
    void* max_bits, void* huft_space, void* hufts_used, void* work_values) {
    constexpr u32 kInflateMaxCodeBits = 15;

    auto* bit_lengths = static_cast<uIntf*>(lengths);
    auto* table = reinterpret_cast<inflate_huft**>(table_out);
    auto* requested_bits = static_cast<uIntf*>(max_bits);
    auto* hufts = static_cast<inflate_huft*>(huft_space);
    auto* used = static_cast<uInt*>(hufts_used);
    auto* values = static_cast<uIntf*>(work_values);
    const auto* bases = static_cast<const uIntf*>(base_values);
    const auto* extras = static_cast<const uIntf*>(extra_bits);
    if (bit_lengths == nullptr || table == nullptr || requested_bits == nullptr ||
        hufts == nullptr || used == nullptr || values == nullptr) {
        return Z_STREAM_ERROR;
    }

    std::array<uInt, kInflateMaxCodeBits + 1> counts{};
    for (u32 index = 0; index < code_count; ++index) {
        if (bit_lengths[index] > kInflateMaxCodeBits) {
            return Z_DATA_ERROR;
        }
        ++counts[bit_lengths[index]];
    }
    if (counts[0] == code_count) {
        *table = static_cast<inflate_huft*>(Z_NULL);
        *requested_bits = 0;
        return Z_OK;
    }

    int table_bits = static_cast<int>(*requested_bits);
    uInt min_length = 1;
    while (min_length <= kInflateMaxCodeBits && counts[min_length] == 0) {
        ++min_length;
    }
    if (static_cast<uInt>(table_bits) < min_length) {
        table_bits = static_cast<int>(min_length);
    }
    uInt max_length = kInflateMaxCodeBits;
    while (max_length != 0 && counts[max_length] == 0) {
        --max_length;
    }
    if (static_cast<uInt>(table_bits) > max_length) {
        table_bits = static_cast<int>(max_length);
    }
    *requested_bits = static_cast<uInt>(table_bits);

    int remaining = 1 << min_length;
    uInt length = min_length;
    for (; length < max_length; ++length, remaining <<= 1) {
        remaining -= static_cast<int>(counts[length]);
        if (remaining < 0) {
            return Z_DATA_ERROR;
        }
    }
    remaining -= static_cast<int>(counts[max_length]);
    if (remaining < 0) {
        return Z_DATA_ERROR;
    }
    counts[max_length] += static_cast<uInt>(remaining);

    std::array<uInt, kInflateMaxCodeBits + 1> offsets{};
    uInt offset = 0;
    offsets[1] = 0;
    for (uInt bits = 1; bits < max_length; ++bits) {
        offset += counts[bits];
        offsets[bits + 1] = offset;
    }

    for (u32 index = 0; index < code_count; ++index) {
        const uInt bits = bit_lengths[index];
        if (bits != 0) {
            values[offsets[bits]++] = index;
        }
    }
    const uInt value_count = offsets[max_length];

    std::array<uInt, kInflateMaxCodeBits + 1> code_stack{};
    std::array<inflate_huft*, kInflateMaxCodeBits> table_stack{};
    uInt huffman_code = 0;
    offsets[0] = 0;
    uIntf* value = values;
    int table_level = -1;
    int decoded_bits = -table_bits;
    table_stack[0] = static_cast<inflate_huft*>(Z_NULL);
    inflate_huft* current_table = static_cast<inflate_huft*>(Z_NULL);
    uInt table_size = 0;

    for (int current_bits = static_cast<int>(min_length);
         current_bits <= static_cast<int>(max_length); ++current_bits) {
        uInt length_count = counts[current_bits];
        while (length_count-- != 0) {
            while (current_bits > decoded_bits + table_bits) {
                ++table_level;
                decoded_bits += table_bits;

                table_size = static_cast<uInt>(
                    static_cast<int>(max_length) - decoded_bits);
                table_size = table_size > static_cast<uInt>(table_bits)
                    ? static_cast<uInt>(table_bits)
                    : table_size;
                uInt entry_bits = static_cast<uInt>(current_bits - decoded_bits);
                uInt patterns = 1u << entry_bits;
                if (patterns > length_count + 1) {
                    patterns -= length_count + 1;
                    uIntf* count_probe = counts.data() + current_bits;
                    if (entry_bits < table_size) {
                        while (++entry_bits < table_size) {
                            patterns <<= 1;
                            if (patterns <= *++count_probe) {
                                break;
                            }
                            patterns -= *count_probe;
                        }
                    }
                }
                table_size = 1u << entry_bits;
                if (*used + table_size > MANY) {
                    return Z_MEM_ERROR;
                }

                current_table = hufts + *used;
                table_stack[table_level] = current_table;
                *used += table_size;
                if (table_level != 0) {
                    code_stack[table_level] = huffman_code;
                    inflate_huft link{};
                    link.word.what.Bits = static_cast<Byte>(table_bits);
                    link.word.what.Exop = static_cast<Byte>(entry_bits);
                    const uInt parent_index =
                        huffman_code >> (decoded_bits - table_bits);
                    link.base = static_cast<uInt>(
                        current_table - table_stack[table_level - 1] -
                        parent_index);
                    table_stack[table_level - 1][parent_index] = link;
                } else {
                    *table = current_table;
                }
            }

            inflate_huft entry{};
            entry.word.what.Bits = static_cast<Byte>(
                current_bits - decoded_bits);
            if (value >= values + value_count) {
                entry.word.what.Exop = 128 + 64;
            } else if (*value < simple_count) {
                entry.word.what.Exop =
                    static_cast<Byte>(*value < 256 ? 0 : 32 + 64);
                entry.base = *value++;
            } else {
                if (bases == nullptr || extras == nullptr) {
                    return Z_DATA_ERROR;
                }
                const uInt complex_index = *value++ - simple_count;
                entry.word.what.Exop = static_cast<Byte>(
                    extras[complex_index] + 16 + 64);
                entry.base = bases[complex_index];
            }

            const uInt fill_step = 1u << (current_bits - decoded_bits);
            for (uInt index = huffman_code >> decoded_bits; index < table_size;
                 index += fill_step) {
                current_table[index] = entry;
            }

            uInt bit = 1u << (current_bits - 1);
            while ((huffman_code & bit) != 0) {
                huffman_code ^= bit;
                bit >>= 1;
            }
            huffman_code ^= bit;

            uInt mask = (1u << decoded_bits) - 1;
            while ((huffman_code & mask) != code_stack[table_level]) {
                --table_level;
                decoded_bits -= table_bits;
                mask = (1u << decoded_bits) - 1;
            }
        }
    }

    return remaining != 0 && max_length != 1 ? Z_BUF_ERROR : Z_OK;
}
int BuildInflateDynamicTrees(u32 literal_count, u32 distance_count, void* lengths,
    void* literal_bits, void* distance_bits, void* literal_tree,
    void* distance_tree, void* hufts, void* stream) {
    return ::inflate_trees_dynamic(static_cast<uInt>(literal_count),
        static_cast<uInt>(distance_count),
        static_cast<uIntf*>(lengths),
        static_cast<uIntf*>(literal_bits),
        static_cast<uIntf*>(distance_bits),
        reinterpret_cast<inflate_huft**>(literal_tree),
        reinterpret_cast<inflate_huft**>(distance_tree),
        static_cast<inflate_huft*>(hufts),
        static_cast<z_streamp>(stream));
}
void GetInflateFixedTrees(void* literal_bits, void* distance_bits,
    void* literal_tree, void* distance_tree, void* stream) {
    ::inflate_trees_fixed(static_cast<uIntf*>(literal_bits),
        static_cast<uIntf*>(distance_bits),
        reinterpret_cast<inflate_huft**>(literal_tree),
        reinterpret_cast<inflate_huft**>(distance_tree),
        static_cast<z_streamp>(stream));
}
int FlushInflateWindow(void* blocks_state, void* stream, int result) {
    return ::inflate_flush(static_cast<inflate_blocks_statef*>(blocks_state),
        static_cast<z_streamp>(stream), result);
}
int ProcessInflateFast(u32 literal_bits, u32 distance_bits, void* literal_tree,
    void* distance_tree, void* blocks_state, void* stream) {
    return ::inflate_fast(static_cast<uInt>(literal_bits),
        static_cast<uInt>(distance_bits),
        static_cast<inflate_huft*>(literal_tree),
        static_cast<inflate_huft*>(distance_tree),
        static_cast<inflate_blocks_statef*>(blocks_state),
        static_cast<z_streamp>(stream));
}

int ZlibInflateBlocksFree113(void* blocks_state, void* stream) {
    return ::inflate_blocks_free(static_cast<inflate_blocks_statef*>(blocks_state),
        static_cast<z_streamp>(stream));
}

}  // namespace ranker
