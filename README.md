# BMW F-Series Cluster + SimHub

Drive a real BMW / Mini F-series instrument cluster with live telemetry from BeamNG.drive (or any other game SimHub supports), using nothing more than an Arduino UNO and a Seeed CAN-BUS Shield V2.

Speedometer, tachometer, gear display, fuel and temperature gauges, blinkers, high beam, handbrake, DSC and check-control warnings all follow the game in real time. Needle movement is interpolated at 50 Hz on the Arduino for smooth sweeps.

## Hardware

- Arduino UNO (or clone)
- Seeed Studio CAN-BUS Shield V2 (MCP2515, 16 MHz crystal, CS on D9)
- BMW F-series instrument cluster (F10/F20/F30 and similar; Mini F5x works too, set `IS_CAR_MINI true`)
- 12 V power supply for the cluster
- PC running SimHub

## Wiring

| Cluster pin | Connect to |
|--|--|
| 1, 2 | +12V |
| 11 | +12V (wake-up signal, cluster stays in standby without it) |
| 7, 8 | GND, shared with the Arduino GND |
| 6 | Shield CAN-H |
| 12 | Shield CAN-L |
| 4, 5 | optional 10k NTC for the outside temperature display |

The shield's onboard 120 Ω termination stays enabled. The bus runs at 500 kbps. The cluster and Arduino must share a common ground.

## Software setup

1. Install the [mcp_can library by coryjfowler](https://github.com/coryjfowler/MCP_CAN_lib) via the Arduino Library Manager
2. Flash `BMW_F_Cluster_SimHub.ino` to the UNO
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

6. For BeamNG.drive: enable OutGauge support in-game (Options → Others → OutGauge, IP `127.0.0.1`, port `4444` — or the SimHub PC's LAN IP if running on a separate machine)

The Arduino prints a `STATUS` line once per second (visible in SimHub's incoming data log or the Serial Monitor at 115200) showing message count and current values, which makes troubleshooting straightforward. With no telemetry, the cluster falls back to 0 km/h and idle rpm.

## What's mapped

| Game | Cluster |
|--|--|
| Speed | Speedometer (1/64 km/h resolution, 50 Hz eased) |
| RPM | Tachometer |
| Gear | P/R/N/D and M1–M9 |
| Water temp (oil temp fallback) | Temperature gauge |
| Fuel level | Fuel gauge incl. low fuel warning |
| Turn signals | Blinker indicators |
| High beam | High beam lamp |
| Handbrake | Parking brake lamp |
| TC intervention | DSC warning |
| Oil pressure / battery warning | Check engine message |
| Game paused | Door open warning |

Known limitations: the cluster shows an SOS malfunction message because the telematics unit is missing — this is normal on the bench and can be dismissed with the BC button or permanently coded out with E-Sys. There is no dedicated ABS lamp in the known check-control set. Fog lights, low beam state and seatbelt status are not available from OutGauge telemetry.

## Credits

The BMW F-series CAN message definitions and CRC scheme are based on the excellent reverse-engineering work in the [CarCluster project by Andrej Rolih](https://github.com/r00li/CarCluster). This project is a UNO/SimHub-focused implementation of that groundwork.

## License

GPL-3.0, same as the CarCluster project this is derived from. See [LICENSE](LICENSE).
