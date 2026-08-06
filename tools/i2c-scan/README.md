# i2c-scan

Standalone PlatformIO sketch that sweeps the reTerminal I²C buses and
prints every 7-bit address that ACKs. E1001-E1004 use their shared bus
on GPIO19/20. E1005 scans both its sensor bus on GPIO1/0 and its separately
powered GT911 touch bus on GPIO3/2.

The tool also monitors all three active-low front buttons. Presses are
reported immediately with their name, GPIO, and uptime.

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
pio run -e reterminal_e1001 -t upload   # or e1002 / e1003 / e1004 / e1005
pio device monitor -b 115200
```

The scanner rescans every 5 s so you can plug and unplug USB and watch
for any address that changes state with charger power. Each sweep and
debounced button press is also appended to `/i2c-scan.log` on the SD card
(when a FAT32/exFAT card is inserted), so the controls can be exercised
without a serial console.

On E1001-E1004, devices in the charger address range (`0x60..0x6F`)
additionally get a 32-register hex dump. E1005 skips this because its
`0x6A` device is the IMU, not a charger.

## Expected devices

| Address | Chip           | Models / bus                                       |
|---------|----------------|----------------------------------------------------|
| `0x14` / `0x5D` | GT911 | E1005 touch bus; E1003 may expose `0x5D`           |
| `0x44`  | SHT4x          | Temperature / humidity sensor                      |
| `0x51`  | PCF8563        | Real-time clock                                    |
| `0x55`  | BQ27220        | E1005 battery fuel gauge                           |
| `0x6A`  | LSM6DS3TR-C    | E1005 IMU                                          |
| `0x6A`  | SY6974B        | E1001-E1004 charger (primary address variant)      |
| `0x6B`  | SY6974B family | E1001-E1004 alternate charger address              |

Anything outside this table is worth investigating.

## Safety

- The address sweep only issues `START + ADDR + STOP` -- no data bytes,
  no chip state changes.
- The register dump helper only *reads*, and only from `0x60..0x6F`.
  The SY6974 / BQ25xxx charger families ignore reads to undefined
  registers, so the sweep does not disturb charger state. Writes are
  what would disable charging or change current limits, so this sketch
  never issues one.
- The 16-bit-register-address devices on the bus (e.g. GT911)
  are skipped by the register-dump helper.
- SD logging opens the file with `FILE_APPEND`, writes, `flush()`es and
  `close()`s on every sweep so the FAT is always consistent -- an unplug
  between sweeps leaves a fully-formed log, not a truncated extend.
