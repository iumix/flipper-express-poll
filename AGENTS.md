# AGENTS.md

Guidance for coding agents and contributors working on **Apple Pay Express Poll**,
a Flipper Zero external app (`.fap`). User-facing docs are in `README.md`; build,
protocol and hardware-test details are in `TECHNICAL.md`.

## Purpose and hard rules

The app sends NFC-F Polling with a configurable system code to wake Express Transit
cards in Apple Wallet, and reads balances using read-only FeliCa commands.

These rules are not negotiable:

- **Read-only.** FeliCa: only send Polling (0x00), Request Service (0x02) and Read
  Without Encryption (0x06). Never add Write (0x08), authentication, or any command
  that changes card state. Balance services must have a read-only, key-free attribute
  (`0x0B`, `0x0F` or `0x17` in the low 6 bits); `tests/` enforces this.
- **ECP selection-only exception.** After ECP and NFC-A identification, RATS/ISO-DEP,
  SELECT PPSE and SELECT of one advertised payment AID are allowed solely to test
  the Wallet animation. Parse only directory AIDs and response status; discard
  selection response bodies. Stop after selection. Never send GPO, READ RECORD,
  GET DATA, GENERATE AC, authentication, or transaction commands. Do not extract,
  log, display or persist payment/cardholder data. ISO-DEP flow control and
  deselection are allowed; all exchanges must be bounded and cancellable.
- **No invented decoders.** Add a balance layout only if it comes from a verified
  source (the reference implementation, a public spec, or a documented hardware
  dump). Never show a value you cannot justify. Report errors instead.
- **No simulated data.** No fake responses, hardcoded balances or placeholder polling.
- **No payment features**: no payment transactions, terminal transaction processing,
  or key/credential extraction. The selection-only exception above is the only EMV use.
- **Honest status.** Do not mark anything as hardware-verified in `README.md`
  unless someone actually ran it on a Flipper and recorded the result.

## Layout

```
application.fam                 Manifest (appid apple_pay_express_poll, entry express_poll_app)
express_poll.png                10x10 1-bit icon
express_poll_app.c / _app_i.h   App struct, alloc/free, ViewDispatcher + SceneManager wiring
felica/felica_frames.[ch]       PURE: build/parse Polling, Request Service, Read; PMm timeouts
felica/felica_systems.[ch]      PURE: system code names, hex parse/format, wildcard check
nfca/ecp_frames.[ch]            PURE: build the Apple ECP frame (NFC-A, no CRC)
nfca/nfca_poll.[ch]             ECP broadcast + NFC-A activation (WUPA + anticollision)
nfca/emv_select.[ch]            PURE: bounded ISO-DEP + selection-only EMV probe
balance/balance.[ch]            PURE: decoder registry + formatting
balance/balance_<system>.c      PURE: one decoder per system
settings/express_poll_settings.[ch]  FlipperFormat load/save (/ext/apps_data/<appid>/settings.txt)
worker/express_poll_worker.[ch] NFC transport + polling state machine (runs on the NfcWorker thread)
views/poll_view.[ch]            Custom View for the poll screen
scenes/                         One file per scene; express_poll_scene_config.h lists them
tests/                          Host unit tests for the PURE modules (Makefile, ASan/UBSan)
.github/workflows/build.yml     CI: host tests + ufbt builds for the dev and release channels
transit-polling/                (git-ignored) separate PN532 reference repo, if checked out here
```

"PURE" files must not include any Flipper header. Use only the C standard
library, because they are compiled on the host by `tests/Makefile`.

## Architecture

- **Two technologies, two modes.** `nfc_config()` binds one technology for the
  whole run and cannot switch mid-run (it requires the idle config state, reached
  only after `nfc_stop`). So FeliCa and NFC-A cannot share one poll loop; the
  worker has a `tech` in its config (`ExpressPollTechFelica` / `ExpressPollTechEcp`)
  and the UI exposes them as two separate menu entries that each start the worker.
- **FeliCa transport:** the worker uses the raw `Nfc` API (`nfc_alloc`,
  `nfc_config(NfcModePoller, NfcTechFelica)`, `nfc_start`, `nfc_poller_trx`) with
  `felica_crc_append/check/trim`. The stock `felica_poller` is unusable here
  because `felica_poller_activate` hardcodes system code `0xFFFF`. Timing
  constants (`FELICA_GUARD_TIME_US`, `FELICA_FDT_POLL_FC`, `FELICA_POLL_POLL_MIN_US`)
  match the firmware poller.
- **ECP transport (NFC-A):** config `NfcTechIso14443a` with the `ISO14443_3A_*`
  timing constants. Each step broadcasts the ECP frame via `nfc_poller_trx` after
  `iso14443_crc_append(Iso14443CrcTypeA, ...)`; ECP is **not answered**, so
  `NfcErrorTimeout` is the success case. It then runs `nfca_poll_activate`
  (WUPA short frame + SDD/SEL anticollision, adapted from the firmware's
  internal `iso14443_3a_poller_activate`, using the exported
  `nfc_iso14443a_poller_trx_short_frame` / `_trx_sdd_frame` primitives) to get
  UID/ATQA/SAK. The selection-only exception above then permits ISO-DEP activation
  and payment application selection. Keep the FeliCa path unchanged.
- **Frames** built by `felica_frames.c` include the LEN byte but no CRC. Response
  buffers are passed with the CRC already trimmed. System codes are big-endian
  in Polling; service codes are little-endian everywhere else.
- **Worker loop:** the firmware calls `worker_nfc_callback` repeatedly with
  `NfcEventTypePollerReady`. Each call runs one step (a poll, plus a balance read
  on a new card) followed by an interruptible wait in 10 ms slices. Phases:
  Searching → Reading → Present → (3 misses) → Searching.
- **Stopping:** in poller mode `nfc_stop()` only *joins* the worker thread. Stopping
  works because the callback returns `NfcCommandStop` once `stop_requested` is
  set. Keep every wait and loop in the callback bounded and checking
  `stop_requested`.
- **Threading:** status is guarded by a mutex. The worker notifies the GUI through
  `view_dispatcher_send_custom_event`, coalesced so that at most one update is
  outstanding (`update_pending`). This matters: the GUI thread blocks in
  `nfc_stop()` while joining, so the NFC thread must never wait on a full queue.
  Never touch views from the NFC thread. Detection feedback (LED/vibro) runs
  on the GUI thread when `detect_count` changes.
- **NFC ownership:** `nfc_alloc()` acquires the NFC HAL. The worker allocates it on
  start and frees it on stop, so the hardware is only held while polling.

## Adding a balance decoder

1. Get the service code, block number and layout from a verified source. Cite it in a comment.
2. Check that the service attribute is read-only and key-free.
3. Add `balance/balance_<name>.c` with `bool balance_<name>_decode(const uint8_t[16], int32_t*)`.
   Return false for implausible data.
4. Declare it in `balance.h`, add a table entry in `balance.c`, add the file to `tests/Makefile`.
5. Add tests with real or reference-derived blocks, including edge and invalid cases.
6. If it should be a preset, update `felica_systems.c` and the system code scene.

## Commands

```sh
ufbt                 # build -> dist/apple_pay_express_poll.fap (warnings are errors)
ufbt launch          # build, upload over USB and run on a connected Flipper
make -C tests        # host unit tests
ufbt cli             # serial console on the device; `log debug` shows FURI_LOG_D output
```

- `ufbt` is installed globally, so don't reinstall or upgrade it unprompted. If a
  Python environment is ever needed, use `uv` and keep it local (`.venv/` is ignored).
- **Match the SDK to the target firmware.** The `.fap` API major version must match
  the device. The locally installed dev snapshot was API 87.0, and release 1.5.1 is
  API 88.2. To build another channel without changing `~/.ufbt`:
  `UFBT_HOME=/tmp/ufbt-release FBT_TOOLCHAIN_PATH=~/.ufbt ufbt update --channel=release`
  and then run `ufbt` with the same variables.

### Formatting

The code follows the firmware's `.clang-format`. **Do not run `ufbt format` while
`transit-polling/` is checked out inside the project.** It reformats every C file
under the app directory, including that separate repository. Run clang-format
from the toolchain on the project's own files instead:

```sh
CF=$(ls ~/.ufbt/toolchain/*/bin/clang-format | head -1)
$CF -i --style=file *.c *.h felica/*.[ch] balance/*.[ch] settings/*.[ch] \
    worker/*.[ch] views/*.[ch] scenes/*.[ch] tests/*.c
```

`application.fam` excludes `tests` and `transit-polling` from `sources`. Keep
those exclusions so host-only code is never linked into the `.fap`.

## Conventions

- C, Flipper firmware style: 4-space indent, `if(` without a space, 99 columns,
  `snake_case` functions prefixed by module (`express_poll_`, `felica_frame_`,
  `balance_`, `poll_view_`), `CamelCase` types and enum values.
- Use `furi_check` for API contract violations and return codes for runtime or card errors.
- Keep comments short and explain *why*. Name protocol constants instead of using bare magic numbers.
- Every allocation has a matching free in the same module. Scenes undo what they
  set up in `on_exit` (`submenu_reset`, `widget_reset`, ...).
- Text must fit 128x64: FontSecondary is about 5 px per character, so keep lines under ~24 characters.

## Testing expectations

- Run `ufbt` (no warnings) and `make -C tests` before committing.
- Hardware-dependent behaviour (Express Mode, real reads, timing, persistence) can
  only be verified on a device. Follow the checklist in `TECHNICAL.md`.
