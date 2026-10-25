# Changelog

Documentation of all notable changes to the **qrvmone** project.

The format is based on [Keep a Changelog],
and this project adheres to [Semantic Versioning].

## [0.12.1] — 2026-10-25

### Fixed

- Configures with CMake 4 on a machine without a populated Hunter cache:
  `cmake/cable/HunterGate.cmake` now generates its download project with
  `cmake_minimum_required(VERSION 3.10)` instead of 3.2, as in
  [cpp-pm/gate#18](https://github.com/cpp-pm/gate/pull/18).

## [0.12.0] — 2026-10-25

### Breaking changes

- Built against QRVMC 3.0.0, ABI version 3, where Zond is revision 0. Hosts need QRVMC 3 headers.

### Fixed

- `execute()` returns `QRVMC_REJECTED` for a revision above
  `QRVMC_MAX_REVISION` instead of indexing the per-revision instruction and
  gas tables past their end. A host built against QRVMC headers from before
  Zond became revision `0` passes `1`, which previously read out of bounds
  silently.
- Byte strings use QRVMC's `qrvmc::bytes` and `qrvmc::bytes_view`, so with a
  QRVMC that provides `byte_traits` the build no longer relies on
  `std::char_traits<uint8_t>`, which libc++ 19 and later deprecate.
- Test builds configure with CMake 4: Hunter's CLI11 and nlohmann_json
  packages are built with `CMAKE_POLICY_VERSION_MINIMUM=3.5`.

## [0.11.0] — 2026-04-23

### Breaking changes

Release aligned to the QRVMC v2.0.0 ABI, which widens the VM word to
512 bits and the QRL address to 64 bytes for the Zond network.

- Stack slot is now `uint512` (512-bit big-endian integers on the
  interpreter stack).
- `qrvmc_bytes32` is 64 bytes wide (wire-level value for SSTORE/SLOAD
  values, log topics, `qrvmc_message.create2_salt`, block/transaction
  context fields).
- `qrvmc_address` is 64 bytes wide (ML-DSA-87 QRL address layout).
- New opcodes `PUSH33`..`PUSH64` at `0x80`..`0x9f`.
- `DUP1`..`DUP16` shifted from `0x80`..`0x8f` to `0xa0`..`0xaf`.
- `SWAP1`..`SWAP16` shifted from `0x90`..`0x9f` to `0xb0`..`0xbf`.
- `LOG0`..`LOG4` shifted from `0xa0`..`0xa4` to `0xc0`..`0xc4`.
- `MLOAD`/`MSTORE` now read/write 64 bytes per word.
- `CALLDATALOAD` reads 64 bytes and zero-pads on short input.
- `BYTE`/`SIGNEXTEND` index 64-byte words (valid range 0..63).
- Generic `addmod`/`mulmod` implementations for `uint512` operands
  (intx only provides native `uint256` specialisations).

### Fixed

- `CREATE2` salt default widened from `uint256{}` to `uint512{}` to
  match the widened stack slot and avoid a mixed-width ternary that
  could silently drop the upper 256 bits if intx conversion rules
  ever shift.
- Dead `load_uint<T>` helper removed; it incorrectly narrowed any
  `qrvmc_bytes32` source via `intx::be::load<uint256>`, which would
  drop the upper 32 bytes of the new 64-byte values.

### Notes

- `QRVMONE_TESTING=ON` requires the `qrvmc-vmtester` tool, which is
  not installed by the bundled qrvmc package. Ship tests stay off in
  release builds until that dependency is resolved.

[0.12.1]: https://github.com/theQRL/qrvmone/releases/tag/v0.12.1
[0.12.0]: https://github.com/theQRL/qrvmone/releases/tag/v0.12.0
[0.11.0]: https://github.com/theQRL/qrvmone/releases/tag/v0.11.0

[Keep a Changelog]: https://keepachangelog.com
[Semantic Versioning]: https://semver.org
