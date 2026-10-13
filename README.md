# SlimeVR 3DS

> **This project was made entirely by an AI** (Claude, by Anthropic): the code, assets, build setup and documentation. It has only been partly tested on real hardware. Use it at your own risk.

A homebrew app that turns any Nintendo 3DS family console (3DS, 3DS XL, 2DS, New 3DS, New 3DS XL, New 2DS XL) into a Wi-Fi [SlimeVR](https://slimevr.dev/) tracker. It uses the console's gyroscope and accelerometer.

- It finds the SlimeVR server on its own, so there's no IP to type.
- It keeps tracking with the lid closed: sleep is blocked, the screens turn off, and the Wi-Fi stays locked on.
- The UI is simple: connection status, gyroscope status, a live 3D view of the console, and a graph of angular velocity.
- The UI is in English, or in French when the console's system language is French.

## Install

| File | Use |
|---|---|
| `dist/SlimeVR-3DS.3dsx` | copy it to `sdmc:/3ds/` and start it from the Homebrew Launcher |
| `dist/SlimeVR-3DS.cia` | install it with FBI (or another installer) to get a HOME Menu icon |
| `dist/SlimeVR-3DS.3ds` | for linkers / flashcarts |

## Usage

1. Turn on the console's Wi-Fi (the console must already know your network) and start the SlimeVR server on your PC.
2. Start the app. Put the console down for about 2 seconds while the gyroscope calibrates.
3. The console looks for the server by itself. The first time, SlimeVR may ask you to **accept the new tracker**.
4. Strap the console to your body, then do the usual SlimeVR resets (full reset, mounting).
5. You can close the console. Tracking keeps going.

Controls: **A** re-centers the 3D view (display only), **SELECT** turns the screens off and on, **START** exits.

### Server discovery

These methods run at the same time:

- a handshake broadcast to `255.255.255.255` and to the subnet broadcast address;
- listening on port 6969, where the SlimeVR server broadcasts its own packets while it has no trackers;
- the last known server, saved in `sdmc:/3ds/SlimeVR-3DS/server.txt`;
- after 3 s, a unicast sweep of the local subnet (at most a /24 on larger networks).

If none of these works (isolated network, VLANs…), put the PC's IP address in `server.txt`.

## How it works

- The app reads gyroscope (~100 Hz) and accelerometer samples straight from the HID shared memory ring buffers.
- A 6-axis Mahony filter estimates the orientation. Gyro bias is calibrated at startup and refined whenever the console is at rest. With no magnetometer, the heading drifts slowly. 6-axis SlimeVR trackers behave the same way, and SlimeVR's resets handle it.
- It speaks SlimeVR's native UDP protocol on port 6969: handshake, sensor info, rotation (packet 17), heartbeat, ping, battery and Wi-Fi signal.
- Sensors and networking run on their own thread, separate from the UI. When the lid is closed (or after SELECT), the UI stops drawing and the backlight turns off.
- To keep Wi-Fi up with the lid closed, the app takes NDM's "infrastructure" exclusive state, locks it, and suspends the StreetPass/SpotPass daemons. If the connection drops anyway, it asks the system to reconnect every 10 s.

## Building

You need devkitPro (`3ds-dev`: devkitARM, libctru, citro2d, citro3d), plus `makerom` and `bannertool` in your PATH.

```sh
./build.sh            # or: make
# with tools outside PATH:
DEVKITPRO=/opt/devkitpro CTRTOOLS=/path/to/makerom_and_bannertool ./build.sh
```

`python3 tools/gen_assets.py` regenerates the icon, banner and jingle (standard library only).

Filter test on PC:

```sh
gcc -O2 -Isource tests/fusion_test.c source/fusion.c -lm -o fusion_test && ./fusion_test
```
