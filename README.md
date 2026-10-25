# Apple Pay Express Poll

A Flipper Zero app that wakes Apple Pay Express Mode on a nearby iPhone or Apple Watch.

- **FeliCa Polling** wakes an Express Transit card such as Suica or Octopus and shows
  its balance. It works with physical transit cards too.
- **ECP Polling** wakes an Express payment card and triggers the Wallet animation.

No custom firmware is needed.

> Not affiliated with or endorsed by Apple, JR East or Octopus Cards Ltd.
> The app only reads. It never makes payments, writes to cards, touches keys or
> reads payment records.

## Install

You need [`ufbt`](https://pypi.org/project/ufbt/) with an SDK that matches your
Flipper's firmware (*Settings → About → Firmware*):

| Firmware | Run once |
|---|---|
| Official release | `ufbt update --channel=release` |
| Official dev build | `ufbt update --channel=dev` |
| Custom firmware | `ufbt update --index-url=<its directory.json>` |

Connect the Flipper over USB, close qFlipper, then build, install and start the app:

```sh
ufbt launch
```

The app is then under *Apps → NFC → Apple Pay Express Poll*. To install it by hand,
run `ufbt` and copy `dist/apple_pay_express_poll.fap` to `/ext/apps/NFC/` on the SD card.

If the Flipper shows "API mismatch", the SDK doesn't match the firmware.

## Usage

1. **Transit:** open **FeliCa Polling**, pick a **System Code**, then **Start Polling**.
   **Payment:** open **ECP Polling**.
2. Hold the iPhone or Apple Watch flat against the back of the Flipper.

**OK** stops and restarts polling. **Back** returns to the menu.

```
FeliCa                          ECP
0x0003 Suica/ICOCA/PASMO        Express Card        ECP A
Present: OK              #57    Present: OK              #41
IDm 012E4CD53A1B220F            UID 04A2B3C4D5E6F7
PMm 100B4B428485D0FF            ATQA 0004 SAK 20
Bal: 1234 JPY                   App selected
           [ Stop ]                        [ Stop ]
```

| System code | Card | Shows |
|---|---|---|
| `0x0003` | Suica / ICOCA / PASMO | Balance in JPY |
| `0x8008` | Octopus | Balance in HKD |
| Custom | Any FeliCa system | IDm and PMm only |

If a balance can't be read, the screen says why instead of showing a number.

**Settings** has the poll interval, the request code, balance reading on/off and
LED/vibro feedback. They are kept across restarts.

## Limitations

- iOS decides whether to present a card. Express Transit or Express payment must be
  turned on for that card in Wallet.
- The ECP frame uses a transit profile that doesn't cover every payment network, so
  some payment cards may not animate.
- Some older physical Octopus cards may show a balance HK$15.0 too low.

## More

Building, protocol details and the hardware test checklist are in
[TECHNICAL.md](TECHNICAL.md).

## Credits

Thanks to [kormax/apple-enhanced-contactless-polling](https://github.com/kormax/apple-enhanced-contactless-polling)
for the reverse-engineering work behind the base ECP frame. Please give the source repo a read.
