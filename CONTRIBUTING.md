# Contributing

Small repository, small rules.

- **Build it first.** `./flash.sh`, or the four steps in the README — a patch
  that does not compile against `gcc-arm-none-eabi 4.8.3` will not be merged.
  If you changed networking code, watch `docker logs mosquitto` for
  connect/drop churn before opening a PR.
- **Say what you measured.** This project prefers a measured number with its
  conditions ("0.45 g trigger against ±0.01 g noise") over a datasheet claim.
  If a change affects the published values, include the before/after reading
  from a real board.
- **Keep it dependency-free.** The MQTT client is hand-written on purpose: the
  prebuilt SDK archives and the 2015-era toolchain leave no room for
  `PubSubClient` and a JSON library. New features should fit the same budget
  (the whole firmware is 37 % of flash, 18 % of RAM).
- **One use case per PR** if it is a firmware feature, and add it to
  `USE_CASES.md` with an effort tag so the table stays honest.
- **No credentials, ever.** `net_config.h` is git-ignored; do not paste SSIDs,
  PSKs, broker passwords or network topology into commits or issues.

Issues with "works on my board" reports are welcome — include the openocd
device id, the board's firmware image and the broker log excerpt.
