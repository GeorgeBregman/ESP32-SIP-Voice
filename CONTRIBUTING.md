# Contributing to ESP32-SIP-Voice

Thanks for your interest in improving ESP32-SIP-Voice! Contributions of all
kinds are welcome — bug reports, hardware test results, documentation, and code.

## Ways to contribute

- **Report a bug / hardware result.** Open an issue with your board, ESP-IDF
  version, tier (LITE / STANDARD / PRO), SIP server (Asterisk, FreePBX, …), and
  a serial log (`idf.py monitor`).
- **Test on hardware.** Real-device validation is the most valuable help right
  now — especially G.722/OPUS interop, ES8388 codec init, and XPT2046 touch
  calibration. Please share the board, panel and outcome.
- **Improve the UI.** Preview and tweak the LVGL themes in the PC simulator
  (`simulator/`) before touching hardware.

## Development setup

- **Firmware:** install [ESP-IDF v4.4+](https://docs.espressif.com/projects/esp-idf/),
  then `idf.py set-target esp32 && idf.py build`.
- **UI simulator (no hardware):** `cmake -S simulator -B simulator/build && cmake --build simulator/build`
  (needs SDL2).
- Every push and pull request is compile-checked by GitHub Actions
  (`.github/workflows/build.yml`) for `esp32` + `esp32s3` and the SDL simulator.

## Pull requests

1. Branch from `main` (`feature/...`, `fix/...`, `docs/...`).
2. Keep PRs focused; describe **what** changed and **why**.
3. Make sure the CI build is green.
4. Match the existing C style (4-space indent, `snake_case`, doc comments on
   public functions). No trailing whitespace.
5. By contributing you agree your work is released under the project's
   [MIT License](LICENSE).

## Code of conduct

Be respectful and constructive. We follow the spirit of the
[Contributor Covenant](https://www.contributor-covenant.org/).
