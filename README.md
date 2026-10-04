# Smart PPE Compliance Kit

Smart PPE Compliance Kit is an embedded safety-compliance system built around an ESP32-S3 vest hub, five ESP32-C3 BLE sensor nodes, and a small web dashboard. The hub collects node status and sensor data for the dashboard, while each shared node sketch is configured with the identity of the PPE item it monitors.

## Nodes

- Left boot
- Right boot
- Left glove
- Right glove
- Helmet

## Toolchain

- Arduino IDE 2.3.10
- ESP32 Arduino core 3.0.7
- NimBLE-Arduino 2.5.1

## Branching

- `main` is stable and tested only. Tag each sprint release (`v0.1.0`, `v0.2.0`, and so on).
- `develop` integrates completed sprint work.
- `feature/sN-short-name` is one branch per sprint task (for example, `feature/s1-ble-spike`). Merge it into `develop` via pull request, then delete the feature branch.
- `fix/short-name` is for bug fixes found during testing.
- Commit messages use one of these prefixes: `feat:`, `fix:`, `docs:`, `chore:`, `test:`.