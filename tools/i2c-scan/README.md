# i2c-scan

Standalone PlatformIO sketch that sweeps the reTerminal E100X shared
I²C0 bus (SDA=GPIO19, SCL=GPIO20) and prints every 7-bit address that
ACKs. Well-known chips are annotated; unexpected devices are flagged.

## When to use it

- Bringing up a new I²C peripheral and want to confirm its address.
- A sensor silently stops responding and you want to check whether it
  fell off the bus entirely or is just misbehaving.
- Confirming which charger IC a given board revision populates
  (V1.2+ E1001/E1002 and all E1003/E1004 ship the SY6974B at `0x6A`;
  earlier E1001/E1002 units have the ETA6003 which has no I²C
  interface).

## Usage

```powershell
cd tools/i2c-scan
pio run -e reterminal_e1001 -t upload   # or e1002 / e1003 / e1004
pio device monitor -b 115200
```

The scanner rescans every 5 s so you can plug and unplug USB and watch
for any address that changes state with charger power. Each sweep is
also appended to `/i2c-scan.log` on the SD card (when a FAT32/exFAT
card is inserted) so you can unplug the USB, exercise the device, and
inspect the log later without needing the serial console.

Devices in the charger address range (`0x60..0x6F`) additionally get a
32-register hex dump so an unknown charger IC can be fingerprinted from
the log alone.

## Expected devices

| Address | Chip           | Notes                                              |
|---------|----------------|----------------------------------------------------|
| `0x44`  | SHT4x          | Temperature / humidity sensor                      |
| `0x51`  | PCF8563        | Real-time clock                                    |
| `0x5D`  | GT911          | Touch controller (E1003 only)                      |
| `0x6A`  | SY6974B        | Battery charger (primary address variant)          |
| `0x6B`  | SY6974B family | Battery charger (alternate address variant, seen on E1003) |

Anything outside this table is worth investigating.

## Safety

- The address sweep only issues `START + ADDR + STOP` -- no data bytes,
  no chip state changes.
- The register dump helper only *reads*, and only from `0x60..0x6F`.
  The SY6974 / BQ25xxx charger families ignore reads to undefined
  registers, so the sweep does not disturb charger state. Writes are
  what would disable charging or change current limits, so this sketch
  never issues one.
- The 16-bit-register-address devices on the bus (e.g. GT911 at 0x5D)
  are skipped by the register-dump helper.
- SD logging opens the file with `FILE_APPEND`, writes, `flush()`es and
  `close()`s on every sweep so the FAT is always consistent -- an unplug
  between sweeps leaves a fully-formed log, not a truncated extend.

