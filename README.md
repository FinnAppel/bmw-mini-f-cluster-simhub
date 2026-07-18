# BMW / Mini F-Series Cluster SimHub

Connect a real BMW / Mini F-series instrument cluster to BeamNG.drive (or any other game supported by SimHub) and display live telemetry using nothing more than an Arduino UNO and a Seeed CAN-BUS Shield V2.

## Hardware

- Arduino UNO (or clone)
- Seeed Studio CAN-BUS Shield V2 (MCP2515, 16 MHz crystal, CS on D9)
- BMW / Mini F-series instrument cluster (see compatibility below)
- 12 V power supply for the cluster
- PC running SimHub

## Cluster compatibility

| Cluster | Status |
| ------- | ------ |
| Mini F56 | ✅ Working |
| Other Mini F5x | ⚠️ Untested |
| BMW F-series (F20, F30, F10, ...) | ⚠️ Untested |
| BMW E-series (E46, E90, E60, ...) | ❌ Unsupported |

⚠️ Untested —  means these clusters use the same CAN messages, so they should work, but nobody has confirmed it yet.

Tested one? Let us know on Discord (see below) or open an issue.

## Wiring

| Cluster pin | Connect to |
|--|--|
| 1, 2 | +12V |
| 11 | +12V (wake-up signal, cluster stays in standby without it) |
| 7, 8 | GND, shared with the Arduino GND |
| 6 | Shield CAN-H |
| 12 | Shield CAN-L |
| 4, 5 | optional 10k NTC for the outside temperature display |

## Software setup

1. Install the [mcp_can library by coryjfowler](https://github.com/coryjfowler/MCP_CAN_lib) via the Arduino Library Manager
2. Flash `BMW_Mini_F_Cluster_SimHub.ino` to the UNO
3. In SimHub, enable the **Custom serial devices** plugin (Settings → Plugins), restart SimHub
4. Custom serial devices → Add new device:
   - Serial port: your Arduino's COM port
   - Baudrate: **115200**
   - DTR enabled
5. Paste the following into the **Update messages** field and set the rate to **30 Hz**:

```
'{"action":10' +
', "spe":' + truncate(format([SpeedKmh], 0)) +
', "gea":"' + isnull([Gear], 'P') + '"' +
', "rpm":' + truncate(isnull([Rpms], 0)) +
', "mrp":' + truncate(isnull([MaxRpm], 8000)) +
', "lft":' + isnull([TurnIndicatorLeft], 0) +
', "rit":' + isnull([TurnIndicatorRight], 0) +
', "oit":' + truncate(isnull([OilTemperature], 0)) +
', "wtr":' + truncate(isnull([WaterTemperature], 0)) +
', "pau":' + isnull([DataCorePlugin.GamePaused], 0) +
', "run":' + isnull([DataCorePlugin.GameRunning], 0) +
', "fue":' + truncate(isnull([DataCorePlugin.Computed.Fuel_Percent], 0)) +
', "hnb":' + isnull([Handbrake], 0) +
', "tra":' + isnull([TCActive], 0) +
', "shl":"' + isnull([DataCorePlugin.GameRawData.ShowLights], '') + '"' +
'}\n'
```

6. For BeamNG.drive: enable OutGauge support in-game (Options → Others → OutGauge, IP `127.0.0.1`, port `63392` — or the SimHub PC's LAN IP if running on a separate machine)

## What's working

- Speedometer
- Tachometer (RPM)
- Turn signals
- Engine warning light
- Gear lever position

## Adding later

- TCS / DSC warning
- ABS
- Handbrake


## Support

Questions, help with your setup, or want to show off your build? Join the **BimmerCraft** Discord: https://discord.gg/bHQ95tqWrw


## Credits

The BMW F-series CAN message definitions and CRC scheme are based on the excellent reverse-engineering work in the [CarCluster project by Andrej Rolih](https://github.com/r00li/CarCluster). This project is a UNO/SimHub-focused implementation of that groundwork.

## License

GPL-3.0, same as the CarCluster project this is derived from. See [LICENSE](LICENSE).
