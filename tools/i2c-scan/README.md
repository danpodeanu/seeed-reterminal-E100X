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
for any address that changes state with charger power.

## Expected devices

| Address | Chip     | Notes                                              |
|---------|----------|----------------------------------------------------|
| `0x44`  | SHT4x    | Temperature / humidity sensor                      |
| `0x51`  | PCF8563  | Real-time clock                                    |
| `0x6A`  | SY6974B  | Battery charger (V1.2+ E1001/E1002, all E1003/E1004) |

Anything outside this table is worth investigating.
