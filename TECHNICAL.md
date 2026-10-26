# Technical notes

Build, protocol and testing details for Apple Pay Express Poll. For installing and
using the app, see [README.md](README.md).

## Building

Requirements: [`ufbt`](https://pypi.org/project/ufbt/) (the official micro Flipper
Build Tool). No other dependencies.

```sh
ufbt            # builds dist/apple_pay_express_poll.fap (warnings are errors)
ufbt launch     # builds, uploads to /ext/apps/NFC/ and starts the app
make -C tests   # host unit tests (needs a C compiler; uses ASan/UBSan)
```

A `.fap` only loads on firmware with the same **API major version** it was built
against. Official release 1.5.x is API 88.

`ufbt` keeps one SDK in `~/.ufbt`, and `ufbt update` replaces it. To build for
a different channel without touching that global state, use a separate
`UFBT_HOME` and reuse the installed toolchain:

```sh
UFBT_HOME=/tmp/ufbt-release FBT_TOOLCHAIN_PATH=~/.ufbt ufbt update --channel=release
UFBT_HOME=/tmp/ufbt-release FBT_TOOLCHAIN_PATH=~/.ufbt ufbt
```

CI (`.github/workflows/build.yml`) runs the host tests and builds against both
the dev and release channels.

## Scope

The app does not charge or deduct money, make payments, emulate a terminal,
write to cards, read keys or credentials, or bypass Apple Pay security.

- FeliCa mode only sends Polling (0x00), Request Service (0x02) and Read Without
  Encryption (0x06). Reads target only services whose attributes mark them as
  read-only and key-free.
- ECP mode allows only ISO-DEP activation, SELECT PPSE, SELECT of one advertised
  payment application, and ISO-DEP flow control/deselection after NFC-A
  identification. It parses directory AIDs and status words only. It never sends
  GPO, READ RECORD, GET DATA, GENERATE AC or authentication commands, and never
  saves or displays selection response bodies.
- T-Money mode sends no ECP frame. After NFC-A identification it allows only
  ISO-DEP activation, SELECT of the T-Money AID, GET BALANCE and ISO-DEP flow
  control/deselection. It parses status words and the 4-byte balance only, and
  discards the SELECT response (which holds the card number). It never sends
  purchase, load, INITIALIZE or record-read commands.

## FeliCa mode

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
  code, which is why it isn't used.
- Request Service runs before the read, so a missing service (`no service`) is
  told apart from a card error (`card err`).
- Read timeouts come from the card's PMm (maximum response time parameters for
  Request Service and Read), plus 50% margin, clamped to 14.7 ms–1 s.
- The field stays on while polling. A card counts as removed after three
  consecutive Polling misses; presence checks run at most every 250 ms. After
  removal, the last data stays on screen until another card is detected.
- Collision handling uses a single time slot. With several FeliCa devices in
  the field you will see `CRC err` instead of a response.

### Balance decoding

| System | Service | Block | Decoding | Currency |
|---|---|---|---|---|
| `0x0003` Suica / ICOCA / PASMO | `0x008B` | 0 | LE uint16 at bytes 11–12 | JPY |
| `0x8008` Octopus | `0x0117` | 0 | (BE int32 at bytes 0–3 − 500) / 10 | HKD |

- Octopus uses the post-2017-10-01 offset of 500, which also covers Apple Pay
  Octopus. Older physical Octopus cards used 350, so their balance would show
  HK$15.0 too low. The app can't tell the two apart.
- The Suica decoder reads the balance only, not the trip history.
- Apple Wallet may not expose the balance service. If the read-only service is
  missing or locked, the app reports `no service` or `card err` instead of a value.

### Balance line

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

Line 2 of the poll screen shows the phase (*Searching*, *Reading* / *Selecting*,
*Present*, *Removed*, *Stopped*), the result of the most recent poll (OK / timeout /
CRC err / bad data / NFC err) and the number of polls sent.

## ECP mode

```
ECP (NFC-A)   TfL transit configuration (+ CRC_A)   → not answered
WUPA          52 (short frame)                      → ATQA
Anticollision 93/95/97 …                            → UID + SAK
RATS          E0 80                                 → ATS
SELECT        2PAY.SYS.DDF01                        → directory AIDs
SELECT        first advertised AID                  → status word
DESELECT                                            → end ISO-DEP
```

- The frame is the documented TfL ECP v2 transit configuration with Express
  payment fallback. It is a specific transit profile, not a universal gate frame,
  and its network mask does not cover every payment network.
- `nfc_config(NfcModePoller, NfcTechIso14443a)`. Each poll appends CRC_A
  (`iso14443_crc_append(Iso14443CrcTypeA, …)`) to the ECP frame and sends it with
  `nfc_poller_trx`. Apple devices never answer ECP itself — they answer the regular
  NFC-A poll that follows — so a timeout on the ECP frame is the expected, successful case.
- Detection reuses the firmware's exported NFC-A primitives
  (`nfc_iso14443a_poller_trx_short_frame` / `_trx_sdd_frame`) for a WUPA +
  anticollision cascade, adapted from the firmware's internal `iso14443_3a_poller_activate`.
  Devices advertising ISO-DEP then enter the selection-only exchange in
  `nfca/emv_select.c`.
- `App selected` means the application returned success. The
  [ECP reference](https://github.com/kormax/apple-enhanced-contactless-polling#ecp-and-express-mode-behavior)
  places the Wallet animation after payment application selection, but the app
  cannot observe the phone screen, so this is not an animation guarantee.
  Selection errors and rejection status words are shown as well.
- The first advertised application is selected; there is no network picker or
  fallback to guessed AIDs. Each SELECT handles bounded response chaining and
  wait-time extensions: responses are limited to 512 bytes, 24 exchanges per SELECT,
  and 300 ms per exchange. Outbound chaining, CID/NAD, retransmission and status-word
  follow-up commands are unsupported. Devices exceeding these limits report an error.
  Stop is checked between exchanges.
- The two technologies can't share one loop (the NFC layer binds one technology per run),
  so FeliCa and ECP are separate menu entries. Both use the same worker, UI,
  interval and feedback settings.

## T-Money mode

```
WUPA          52 (short frame)                      → ATQA      (no ECP frame)
Anticollision 93/95/97 …                            → UID + SAK
RATS          E0 80                                 → ATS
SELECT        00 A4 04 00 07 D4100000030001 00      → status word (body discarded)
GET BALANCE   90 4C 00 00 04                        → 4-byte balance + 9000
DESELECT                                            → end ISO-DEP
```

- Apple Wallet doesn't use ECP for T-Money. The
  [ECP reference](https://github.com/kormax/apple-enhanced-contactless-polling#express-mode-types-and-priority)
  lists it as a *GenericA* pass: it answers plain NFC-A polling, and any ECP frame
  (which has higher priority) suppresses it. That is why T-Money is a separate mode
  rather than part of ECP mode. Only one of T-Money and T-Union can be the Express
  card on a device.
- The same reference gives T-Money's success condition as "DESELECT/TRESET after
  SELECT + any successful protocol command". Selection alone would end as a failed
  read, so the app sends exactly one read-only command, GET BALANCE, then deselects.
- AID and GET BALANCE come from Proxmark3 (`client/src/ksx6924/ksx6924core.c`,
  `KSX6924Select` / `KSX6924GetBalance`) and the
  [Metrodroid South Korea notes](https://github.com/metrodroid/metrodroid/wiki/South-Korea):
  the reply is a 4-byte big-endian balance in won. The app requires exactly 4 bytes
  and `9000`, and rejects values above 500,000 KRW. That limit is the T-money maximum
  load from the (uncited) [Wikipedia infobox](https://en.wikipedia.org/wiki/T-money),
  so it's only a sanity check. Snapper (New Zealand) shares the AID but stores cents,
  so it isn't supported.
- GenericA needs roughly 72–77 ms of field time before the phone answers. The field
  stays on between polls, so the first poll or two may time out before the phone answers.
- RATS is sent even when the SAK doesn't advertise ISO-DEP, since some dual-interface
  cards still answer it. An unanswered RATS shows `No RATS reply (SAK xx)`; an ATS the
  app can't use shows `ATS unsupported`.
- ISO-DEP handling (`nfca/iso_dep.c`) is shared with ECP mode and has the same
  bounds: 512-byte responses, 24 exchanges per APDU, 300 ms per exchange, and Stop is
  checked between exchanges. Any ATS frame size is accepted as long as each APDU fits
  in it, and a declared frame wait time above 300 ms is capped rather than refused. After a read the field is reset, and presence checks
  are WUPA only, so a phone held in place is read once.
- The *Read balance* setting only applies to FeliCa: T-Money always sends GET
  BALANCE, because Wallet needs it to complete the read.

## Settings

- *Poll interval*: 50 / 100 / 250 / 500 / 1000 ms between polls (default 100 ms)
- *Request code*: `Sys code` (RC=0x01, default) asks the card to append its system
  code. `None` sends RC=0x00.
- *Read balance*: On / Off (FeliCa only)
- *LED/vibro*: blink while polling, plus a success signal on each detection

The custom system code editor accepts any 16-bit value, including wildcards such
as `0xFFFF`. Settings are stored in `/ext/apps_data/apple_pay_express_poll/settings.txt`
(a FlipperFormat text file; delete it to reset).

## Hardware test checklist

Record the firmware version, the device model and iOS/watchOS version, and the
Wallet card for each run.

1. **Install**: `ufbt launch` and check that the app opens with no API mismatch.
2. **Persistence**: choose Custom `0x12FC`, change the interval to 250 ms, exit the app,
   reopen it, and check that *FeliCa Polling* shows `System Code: 0x12FC` and that
   Settings kept 250 ms.
3. **Start/stop**: start polling. The counter should increase and the cyan LED should blink.
   Press OK to stop (LED off, counter frozen), then OK again to restart. Press Back while
   polling: the menu should appear right away and the NFC field should be off (a card held
   to the Flipper no longer reacts).
4. **Physical Suica/PASMO/ICOCA card** (`0x0003`): you should get `Present: OK`, IDm/PMm
   and a balance. Compare it with a ticket machine.
5. **Physical Octopus card** (`0x8008`): compare with an Octopus reader.
6. **iPhone, Express Transit Suica** (screen locked, `0x0003`): note whether the card UI
   appears, whether IDm/PMm appear, and what the balance line shows. Repeat with the screen on.
7. **iPhone, Express Transit Octopus** (`0x8008`): same as step 6.
8. **Apple Watch**: repeat steps 6 and 7.
9. **ECP**: choose *ECP Polling*, hold an iPhone/Watch with an Express payment card near
   the Flipper, and note whether it presents the card and whether the screen shows `UID`,
   `ATQA`/`SAK` and a selection result. Record any rejection status word. For
   `App selected`, separately record whether a card/spinner appears; selection success
   alone is not animation verification. Try with the screen locked and on, remove and
   re-present the device, and press Stop during selection to check responsiveness.
10. **T-Money**: choose *T-Money Polling*. With a physical T-Money card, compare the
    balance with a top-up machine or the Tmoney app. With an iPhone/Watch where
    T-Money is the Express Transit card, note whether the card and spinner appear,
    whether a check mark follows, and whether `Bal:` matches Wallet. Try with the screen
    locked and on, remove and re-present, and press Stop during a read. Record any
    rejection status word. If no check mark appears, capture `log debug` output.
11. **Negative cases**: an unsupported FeliCa card with `0x0003` (expect `Searching: timeout`),
    wildcard `0xFFFF` with RC=None (`System unknown`), balance off (`bal. off`), pulling the
    card away mid-read (`no response` or `invalid data`, followed by `Removed`), and ECP mode
    with nothing nearby (`Searching: timeout`), and T-Money mode with an EMV-only
    card (`No T-Money app`).
12. **Soak**: poll for 10 minutes in each mode and check that the counter keeps rising and
    that the app exits cleanly.
