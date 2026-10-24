# Apple Pay Express Poll

A Flipper Zero app by **iumix** that sends NFC-F (FeliCa) Polling commands with a
configurable 16-bit system code. Its main purpose is to wake an Express Transit
card in Apple Wallet on an iPhone or Apple Watch. For recognised systems it then
tries to read the stored balance using read-only FeliCa commands.

It is a Flipper port of the PN532/libnfc tools in
[transit-polling](https://github.com/iumix/transit-polling).

> Not affiliated with or endorsed by Apple, JR East or Octopus Cards Ltd.
> The app never makes payments, never writes to a card and never touches keys.

## Features

- NFC-F Polling (SENSF_REQ) with any system code `0x0000`–`0xFFFF`
- Presets for `0x0003` (Suica / ICOCA / PASMO) and `0x8008` (Octopus), plus a custom hex code
- The selected code and all settings persist across restarts
- Start/stop from the UI. Polling runs on the NFC worker thread, so the UI never blocks
- Shows the polled code, phase, per-poll result (OK / timeout / CRC err / bad data / NFC err),
  a poll counter, IDm, PMm and the balance or the reason no balance is shown
- Detects card removal and re-reads the card when it comes back
- Read-only balance for:

  | System | Service | Block | Decoding | Currency |
  |---|---|---|---|---|
  | `0x0003` Suica / ICOCA / PASMO | `0x008B` | 0 | LE uint16 at bytes 11–12 | JPY |
  | `0x8008` Octopus | `0x0117` | 0 | (BE int32 at bytes 0–3 − 500) / 10 | HKD |

  The layouts come from the reference implementation (`suica_balance.c`, `octopus_balance.c`).

## Verification status

| Item | Implemented | Automated tests | Physical hardware |
|---|---|---|---|
| `.fap` builds (dev SDK API 87.0, release 1.5.1 API 88.2), zero warnings | ✅ | ✅ `ufbt` + `APPCHK` | – |
| Polling frame construction (`0x0003`, `0x8008`, custom, RC, TSN) | ✅ | ✅ byte-exact against the reference | ✅ `0x0003`, `0x8008` · ⏳ custom codes |
| SENSF_RES parsing (IDm, PMm, optional system code, malformed frames) | ✅ | ✅ | ✅ |
| Request Service / Read Without Encryption frames and parsing | ✅ | ✅ byte-exact against the reference | ✅ |
| PMm-derived response timeouts | ✅ | ✅ | ✅ (iPhone and physical Suica) |
| Suica / Octopus balance decoding and formatting | ✅ | ✅ | ✅ |
| System code hex parse/format, wildcard detection | ✅ | ✅ (all 65,536 values round-trip) | – |
| Settings persistence across restarts | ✅ | ❌ (needs the Flipper storage API) | ⏳ pending |
| Start/stop, non-blocking UI, safe shutdown | ✅ | ❌ | ⏳ pending |
| Waking Apple Pay Express Mode on iPhone / Apple Watch | ✅ | ❌ | ✅ iPhone 18 Pro: Suica `0x0003`, Octopus `0x8008` · ⏳ Apple Watch |
| Balance read from Apple Wallet Suica / Octopus | ✅ | ❌ | ✅ iPhone 18 Pro: Suica and Octopus · ⏳ Apple Watch |
| Balance read from physical Suica / Octopus cards | ✅ | ❌ | ✅ physical Suica · ⏳ physical Octopus |

Hardware results reported by the author on 2026-10-24, using a Flipper Zero
and an iPhone 18 Pro: Express Mode activation and balance reads work for Apple
Wallet Suica (`0x0003`) and Octopus (`0x8008`), and balance reads work for a
physical Suica card. Apple Watch, physical Octopus cards, custom system codes,
settings persistence and the negative cases in the procedure below have not
been reported yet.

## Installation

### Pick a matching SDK first

A `.fap` only loads on firmware with the same **API major version** it was built
against. If they differ, the Flipper shows "API mismatch".

| Firmware on the Flipper | Build with |
|---|---|
| Official release (e.g. 1.5.x, API 88) | `ufbt update --channel=release` |
| Official dev builds | `ufbt update --channel=dev` |
| Custom firmware | `ufbt update --index-url=<its directory.json>` (or its own SDK) |

Check the device under *Settings → About → Firmware* (or in qFlipper).

### Install over USB (recommended)

Connect the Flipper over USB, close qFlipper and any serial console, then:

```sh
ufbt launch      # builds, uploads to /ext/apps/NFC/ and starts the app
```

### Install manually

1. Build (see below), or download a `.fap` from a CI run.
2. Copy `dist/apple_pay_express_poll.fap` to the SD card as
   `/ext/apps/NFC/apple_pay_express_poll.fap` (with qFlipper's file manager or a card reader).
3. On the Flipper, open *Apps → NFC → Apple Pay Express Poll*.

## Building

Requirements: [`ufbt`](https://pypi.org/project/ufbt/) (the official
micro Flipper Build Tool). No other dependencies.

```sh
ufbt            # builds dist/apple_pay_express_poll.fap
make -C tests   # host unit tests (needs a C compiler; uses ASan/UBSan)
```

`ufbt` keeps one SDK in `~/.ufbt`, and `ufbt update` replaces it. To build for
a different channel without touching that global state, use a separate
`UFBT_HOME` and reuse the installed toolchain:

```sh
UFBT_HOME=/tmp/ufbt-release FBT_TOOLCHAIN_PATH=~/.ufbt ufbt update --channel=release
UFBT_HOME=/tmp/ufbt-release FBT_TOOLCHAIN_PATH=~/.ufbt ufbt
```

CI (`.github/workflows/build.yml`) runs the host tests and builds against both
the dev and release channels.

## Usage

**Main menu**

- **Start Polling** opens the poll screen and starts polling straight away.
- **System Code: 0x….** opens the presets `0x0003` and `0x8008`, or *Custom*.
  Custom shows a two-byte hex editor. Any 16-bit value is accepted, including
  wildcards such as `0xFFFF`.
- **Settings**
  - *Poll interval*: 50 / 100 / 250 / 500 / 1000 ms between Polling commands (default 100 ms,
    the same as the reference)
  - *Request code*: `Sys code` (RC=0x01, default, the same as the reference) asks the card to
    append its system code. `None` sends RC=0x00.
  - *Read balance*: On / Off
  - *LED/vibro*: blink while polling, plus a success signal on each detection
- **About**

**Poll screen**

```
0x0003 Suica/ICOCA/PASMO
Present: OK              #57
IDm 012E4CD53A1B220F
PMm 100B4B428485D0FF
Bal: 1234 JPY
           [ Stop ]
```

- Line 2 shows the phase (*Searching*, *Reading*, *Present*, *Removed*, *Stopped*), the result of
  the most recent Polling command and the number of polls sent.
- **OK** starts or stops polling. **Back** stops polling and returns to the menu.
- After the card is removed, its last data stays on screen until another card is detected.

Possible balance lines:

| Text | Meaning |
|---|---|
| `Bal: 1234 JPY` | Decoded successfully |
| `Sys 0x…: no decoder` | Card answered, but there is no verified balance layout for that system |
| `System unknown` | Wildcard poll with RC=None, so the system cannot be identified |
| `Bal: no service` | Request Service reports that the balance service does not exist |
| `Bal: card err XX/YY` | Read returned status flags SF1/SF2 (for example `01/A6` = illegal service code) |
| `Bal: no response` | The read timed out (common if the phone/card was moved away) |
| `Bal: invalid data` | Malformed response, bad CRC or an implausible value |
| `Sys 0x… (bal. off)` | Balance reading is disabled in Settings |

Settings are stored in `/ext/apps_data/apple_pay_express_poll/settings.txt`
(a FlipperFormat text file; delete it to reset).

## How it works

```
Polling   06 00 SC_hi SC_lo RC 00        → SENSF_RES: IDm, PMm[, system code]
Request   0D 02 IDm 01 SV_lo SV_hi       → key version (FFFF = service missing)
 Service
Read      10 06 IDm 01 SV_lo SV_hi 01 80 blk → SF1 SF2 01 <16-byte block>
w/o Enc
```

(These frames are shown without the 2-byte CRC, which the transport appends.)

- The app uses the firmware's raw `Nfc` transport in poller mode
  (`nfc_alloc` / `nfc_config(NfcModePoller, NfcTechFelica)` / `nfc_poller_trx`)
  with the exported `felica_crc_*` helpers. The firmware's high-level FeliCa
  poller always polls the wildcard `0xFFFF` and cannot send a custom system
  code, which is why it isn't used. **No custom firmware is required.**
- Read timeouts come from the card's PMm (maximum response time parameters for
  Request Service and Read), plus 50% margin, clamped to 14.7 ms–1 s. The
  PN532 version relied on libnfc's defaults.
- The field stays on while polling. A card counts as removed after three
  consecutive Polling misses; presence checks run at most every 250 ms.

### Differences from the PN532 reference

| PN532 / libnfc | Flipper |
|---|---|
| `nfc_initiator_select_passive_target` with SENSF_REQ | Hand-built Polling frame via `nfc_poller_trx` |
| PN532 adds the CRC | Software CRC (`felica_crc_append/check/trim`) |
| `nfc_initiator_target_is_present` + 0x04 command | Re-poll and compare IDm |
| Blind read, then check status | Request Service first (`no service` vs. `card err`), then read |
| libnfc default timeouts | PMm-derived per-command timeouts |

## Limitations

- **Express Mode is not guaranteed.** iOS decides whether to present a card.
  Express Transit must be enabled for that card in Wallet, and the device may
  need the right region settings. Some transit readers also send Apple's
  Enhanced Contactless Polling (ECP) frames over NFC-A; this app sends only
  NFC-F Polling. Waking Express Mode does not mean a
  balance can be read.
- **Apple Wallet may not expose the balance service.** If the read-only
  service is missing or locked on the device, the app reports `no service` or
  `card err` instead of a value.
- Only `0x0003` and `0x8008` have balance decoders. Other systems show IDm/PMm only.
- Octopus uses the post-2017-10-01 offset of 500, which also covers Apple Pay
  Octopus. Older physical Octopus cards used 350, so their balance would show
  HK$15.0 too low. The app can't tell the two apart.
- The Suica decoder reads the balance only, not the trip history.
- Collision handling uses a single time slot. With several FeliCa devices in
  the field you will see `CRC err` instead of a response.
- The Flipper's antenna is small, so hold the device flat against the back of the Flipper.

## Hardware test procedure

Record the firmware version, the device model and iOS/watchOS version, and the Wallet card for each run.

1. **Install**: `ufbt launch` and check that the app opens with no API mismatch.
2. **Persistence**: choose Custom `0x12FC`, change the interval to 250 ms, exit the app, reopen
   it, and check that the main menu shows `0x12FC` and that Settings kept 250 ms.
3. **Start/stop**: start polling. The counter should increase and the cyan LED should blink.
   Press OK to stop (LED off, counter frozen), then OK again to restart. Press Back while polling:
   the menu should appear right away and the NFC field should be off (a card held to the Flipper
   no longer reacts).
4. **Physical Suica/PASMO/ICOCA card** (`0x0003`): you should get `Present: OK`, IDm/PMm and a
   balance. Compare it with a ticket machine or the reference `suica_balance`.
5. **Physical Octopus card** (`0x8008`): compare with an Octopus reader or the reference
   `octopus_balance`.
6. **iPhone, Express Transit Suica** (screen locked, `0x0003`): note whether the card UI
   appears, whether IDm/PMm appear, and what the balance line shows. Repeat with the screen on.
7. **iPhone, Express Transit Octopus** (`0x8008`): same as step 6.
8. **Apple Watch**: repeat steps 6 and 7.
9. **Negative cases**: an unsupported FeliCa card with `0x0003` (expect `Searching: timeout`),
   wildcard `0xFFFF` with RC=None (`System unknown`), balance off (`bal. off`), and pulling the
   card away mid-read (`no response` or `invalid data`, followed by `Removed`).
10. **Soak**: poll for 10 minutes and check that the counter keeps rising and that the app exits
    cleanly.

Report the results in the verification table above.

## Out of scope

The app does not charge or deduct money, make payments, emulate a terminal,
write to cards, read keys or credentials, or bypass Apple Pay security. It
only sends the FeliCa commands Polling (0x00), Request Service (0x02) and
Read Without Encryption (0x06), and the read targets only services whose
attributes mark them as read-only and key-free.

## License / credits

Author: iumix. Protocol logic and balance layouts are adapted from
[iumix/transit-polling](https://github.com/iumix/transit-polling).
