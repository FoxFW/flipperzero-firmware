# Sub-GHz protocol authoring with `common.c`

**Read this before adding or migrating any sub-GHz protocol.** Adding protocols is the
main ongoing job, so this is the canonical pattern — keep it solid.

## Why `common.c` exists
Every protocol used to carry its own copy of the same ~8 vtable functions
(`free / reset / stop / yield / get_hash_data / serialize / deserialize / alloc`). Across
~160 protocol files that was ~12,000 lines of duplicated boilerplate and a large amount of
internal flash. `common.c` holds ONE copy of each; a protocol points its vtable slots
straight at the shared function. This is how we fit far more protocols into internal flash
instead of externalising them as `.fal`.

**Rule of thumb: a new protocol writes only what is genuinely protocol-specific** — the
decode state machine (`feed`), the `get_upload`, the `get_string`, and any extra persisted
fields. Everything else points at `common_*`.

## The one hard rule — struct layout
The shared functions reach your instance through a `void*` and only touch the leading
members, so your structs MUST start with this exact prefix:

```c
struct SubGhzProtocolDecoderX {
    SubGhzProtocolDecoderBase base;     // 1st
    SubGhzBlockDecoder        decoder;  // 2nd
    SubGhzBlockGeneric        generic;  // 3rd
    uint32_t te;                        // 4th — ONLY if you use the TE helpers
    /* ... your own fields after ... */
};

struct SubGhzProtocolEncoderX {
    SubGhzProtocolEncoderBase  base;     // 1st
    SubGhzProtocolBlockEncoder encoder;  // 2nd
    SubGhzBlockGeneric         generic;  // 3rd
    /* ... your own fields after ... */
};
```

Immediately after the struct definitions (file scope, not inside a function) add the
compile-time guard. If the prefix is wrong the BUILD FAILS — it can never silently corrupt
memory:

```c
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderX);  // decoder without te
SUBGHZ_ASSERT_DECODER_TE_LAYOUT(SubGhzProtocolDecoderX);      // decoder WITH te (use one or the other)
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderX); // encoder
```

## Which vtable slot uses common vs. your own
| slot | use | notes |
|---|---|---|
| decoder `.free` | `subghz_protocol_decoder_common_free` | always, unless you malloc extra buffers in the decoder |
| decoder `.reset` | `..._common_reset` | only if reset just sets `parser_step=0`. If it also clears your fields → keep custom (see princeton: clears `last_data`). |
| decoder `.get_hash_data` | `..._common_get_hash_data` | standard. Rolling-counter remotes keyed on serial^cnt → `..._common_get_hash_data_serial`. |
| decoder `.serialize` | `..._common_serialize` | if you persist nothing beyond the generic block. One extra field → `..._common_serialize_te` (adds TE) or call `..._common_serialize` then append yours (the "helper + tail" pattern — see princeton Guard_time). |
| decoder `.deserialize` | usually **custom** | needs your `min_count_bit` and any range check / extra fields. `..._common_deserialize_te` covers the count-bit check + TE read if that's all you need. |
| decoder `.alloc` | **wrapper** | `return subghz_protocol_decoder_common_alloc(sizeof(SubGhzProtocolDecoderX), &subghz_protocol_X);` |
| decoder `.feed` / `.get_string` | **custom** | this is your protocol |
| encoder `.free` | `..._encoder_common_free` | frees `encoder.upload` + instance |
| encoder `.stop` | `..._encoder_common_stop` | always |
| encoder `.yield` | `..._encoder_common_yield` | always |
| encoder `.deserialize` | **wrapper** or custom | plain count-bit + Repeat + get_upload → `..._encoder_common_deserialize(ctx, ff, min_count_bit, (SubGhzProtocolEncoderGetUpload)your_get_upload)`. Reads extra fields (TE/Guard) or a bit-count range check → keep custom. |
| encoder `.alloc` | **wrapper** | `return subghz_protocol_encoder_common_alloc(sizeof(SubGhzProtocolEncoderX), &subghz_protocol_X, repeat, size_upload);` |

## SOLID checklist / gotchas
1. `common_alloc` sets `generic.protocol_name` for you — don't set it again, and don't drop it (the shared funcs log through it).
2. The `get_upload` passed to `common_deserialize` is `static bool f(SubGhzProtocolEncoderX*)`; **cast it** to `(SubGhzProtocolEncoderGetUpload)` or `-Werror` rejects the pointer-type mismatch.
3. Layout asserts go at file scope right after the struct defs.
4. Keeping a custom `reset`/`serialize`/`deserialize` is correct when you have extra state — use common for everything else.
5. A `.sub` saved by the old build MUST still load and re-transmit identically after migration — this refactor is behaviour-preserving. Verify (host harness + on-device) before shipping.

## Worked examples already in this folder
- **`gate_tx.c`** — the maximal case: `reset` + `serialize` fully common, encoder `deserialize`
  is a `common_deserialize` wrapper. **Copy this for a new simple static protocol.**
- **`nice_flo.c`** — like gate_tx but both `deserialize`s stay custom (a bit-count range check).
- **`princeton.c`** — a TE protocol: `SUBGHZ_ASSERT_DECODER_TE_LAYOUT`, `serialize` =
  `common_serialize_te` + a Guard_time tail, custom `reset` (clears `last_data`).

## Skeleton — new simple static protocol (gate_tx-shaped)
```c
#include "myproto.h"
#include "common.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolMyProto"

static const SubGhzBlockConst subghz_protocol_myproto_const = {
    .te_short = 350, .te_long = 700, .te_delta = 100, .min_count_bit_for_found = 24,
};

struct SubGhzProtocolDecoderMyProto { SubGhzProtocolDecoderBase base; SubGhzBlockDecoder decoder; SubGhzBlockGeneric generic; };
struct SubGhzProtocolEncoderMyProto { SubGhzProtocolEncoderBase base; SubGhzProtocolBlockEncoder encoder; SubGhzBlockGeneric generic; };

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderMyProto);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderMyProto);

typedef enum { MyProtoDecoderStepReset = 0, /* ... */ } MyProtoDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_myproto_decoder = {
    .alloc = subghz_protocol_decoder_myproto_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_myproto_feed,
    .reset = subghz_protocol_decoder_common_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_myproto_deserialize,
    .get_string = subghz_protocol_decoder_myproto_get_string,
};
const SubGhzProtocolEncoder subghz_protocol_myproto_encoder = {
    .alloc = subghz_protocol_encoder_myproto_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_myproto_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};
const SubGhzProtocol subghz_protocol_myproto = { .name = SUBGHZ_PROTOCOL_MYPROTO_NAME,
    .type = SubGhzProtocolTypeStatic, .flag = /* ... */ SubGhzProtocolFlag_Decodable,
    .decoder = &subghz_protocol_myproto_decoder, .encoder = &subghz_protocol_myproto_encoder, };

void* subghz_protocol_decoder_myproto_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(sizeof(SubGhzProtocolDecoderMyProto), &subghz_protocol_myproto);
}
void* subghz_protocol_encoder_myproto_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(sizeof(SubGhzProtocolEncoderMyProto), &subghz_protocol_myproto, 3, 52);
}
static bool subghz_protocol_encoder_myproto_get_upload(SubGhzProtocolEncoderMyProto* instance) { /* build instance->encoder.upload[...] */ return true; }
SubGhzProtocolStatus subghz_protocol_encoder_myproto_deserialize(void* context, FlipperFormat* ff) {
    return subghz_protocol_encoder_common_deserialize(context, ff,
        subghz_protocol_myproto_const.min_count_bit_for_found,
        (SubGhzProtocolEncoderGetUpload)subghz_protocol_encoder_myproto_get_upload);
}
SubGhzProtocolStatus subghz_protocol_decoder_myproto_deserialize(void* context, FlipperFormat* ff) {
    SubGhzProtocolDecoderMyProto* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(&instance->generic, ff, subghz_protocol_myproto_const.min_count_bit_for_found);
}
void subghz_protocol_decoder_myproto_feed(void* context, bool level, uint32_t duration) { /* your state machine */ }
void subghz_protocol_decoder_myproto_get_string(void* context, FuriString* output) { /* your display */ }
```

---

## 2026-10-21 — maximum dedup pass (v2.1 "all live")

This pass pushed the common-file dedup as far as is safe. Three layers now exist:

### Layer 1 — `common.c` vtable boilerplate (all protocols)
Reclaimed **208 vtable slots across 83 files** that earlier exact-match passes
had skipped because of *idiom differences*, not real behaviour differences:
- `furi_check(context)` vs `furi_assert(context)`
- `free(context)` vs `free(instance)`
These are behaviourally identical, so they now point at the shared handlers.

Slots migrated and their rule:
| slot | shared fn | requires standard layout? |
|------|-----------|---------------------------|
| decoder `.free` | `subghz_protocol_decoder_common_free` | NO — just frees `context` |
| decoder `.get_hash_data` | `..._common_get_hash_data` | YES (reads `->decoder`,`->generic`) |
| decoder `.serialize` | `..._common_serialize` | YES (reads `->generic`) |
| encoder `.free` | `subghz_protocol_encoder_common_free` | YES (reads `->encoder.upload`) |
| encoder `.stop` | `..._common_stop` | YES (reads `->encoder.is_running`) |
| encoder `.yield` | `..._common_yield` | YES (reads `->encoder.*`) |

**Only `decoder.free` is layout-independent** (it just `free()`s the pointer).
Everything else dereferences `base/decoder/generic` (decoder) or `base/encoder`
(encoder), so the struct MUST begin with that exact member sequence. The
`SUBGHZ_ASSERT_*_LAYOUT` macro is added next to every migrated struct and will
**fail the build loudly** if the layout is ever reordered — never silently
corrupt. Do not migrate a layout-dependent slot without the assert.

### Non-standard-layout files — do NOT migrate layout slots
These files put `generic` before `decoder`, or otherwise differ. They may use
`common_free` (layout-independent) but **keep their own** hash/serialize/
enc-free/stop/yield:
`fiat_spa, fiat_v2_v3_v5, honda_static, honda_v1, honda_v2_v3, honeywell,
mercedes_fob, porsche_cayenne, raw, vag`.
When adding a protocol, verify your decoder struct is `base→decoder→generic`
and encoder is `base→encoder` before pointing a layout slot at common; if not,
keep that slot local (and do not add the assert, it would fail).

### `alloc` is intentionally NOT shared
Many protocols `calloc(1, sizeof(...))` (zero-init) while
`subghz_protocol_decoder_common_alloc` uses `malloc` (NOT zeroed). Migrating
alloc blindly would introduce uninitialised-memory bugs. Leave alloc custom
unless you have verified the protocol does not rely on zero-init.

### Layer 2 — `pp_auto_common.c` (`ppa_` helpers, 6 ProtoPirate-derived autos)
`alps_rke, code_alarm, mic6sc2, hyundai_kia_rio, porsche_touareg, santa_fe`
previously inlined 16 byte-identical `static inline pp_*` helpers each (they
lived in separate .fal groups with no shared .c). Those are now one shared copy
in `pp_auto_common.c` under the **`ppa_`** prefix, cast against the common
`SubGhzProtocolDecoderCommon`/`EncoderCommon` types.

**`ppa_` is deliberately distinct from the `pp_` helpers in
`protocols_common.c`.** Those are a *different lineage with different
behaviour* (e.g. `pp_serialize_fields` there uses write-with-error-check; the
`ppa_` version uses update-or-insert). They are NOT interchangeable — do not
merge the two without on-hardware save/load testing of all affected protocols.

### Gotcha — layout assert placement vs. the bare typedef
`SUBGHZ_ASSERT_*_LAYOUT(Type)` expands to `offsetof(Type, base)`, so the **bare
type name must already be a visible typedef** at the assert's line. Two patterns
exist in this tree:
- typedef in the `.h` (e.g. honda_v10) → the bare name is visible anywhere in the
  `.c`; put the assert right after the struct.
- `struct X {...};` in the `.c` with `typedef struct X X;` **later** in the same
  `.c` (the ex-`protocols_ext` autos: audiovox, ford_v0/v2/v3, fiat_v2, honda_v2,
  land_rover_v0, mazda_v0, renault_v0) → an assert placed right after the struct
  body fails with *"unknown type name"*. Put the assert **after** the typedef
  (these files now carry their asserts at EOF for that reason).
When adding a protocol, place the layout assert where its bare typedef is already
in scope.
