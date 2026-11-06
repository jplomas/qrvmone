# Changelog

Documentation of all notable changes to the **qrvmone** project.

The format is based on [Keep a Changelog],
and this project adheres to [Semantic Versioning].

## [Unreleased]

### Added

- `qrvmone-conformance`, a runner for the go-qrl VM conformance corpus
  (`core/vm/conformance` in go-qrl). It reads vectors as tab separated lines on
  stdin and prints return data, error class and gas used, so go-qrl's
  `TestGoVMMatchesQrvmone` (driven by `hack/vm-conformance.sh` there) can
  compare both runtimes vector by vector. Built with `QRVMONE_TESTING=ON`.
  From [cyyber/qrvmone#9](https://github.com/cyyber/qrvmone/pull/9).

### Fixed

- `SLOAD` and `SSTORE` key contract storage by the low 256 bits of the
  64-byte stack operand, matching go-qrl, which keys state storage by 32-byte
  hashes. The upper 32 bytes of the key are zeroed before it reaches the
  host's `get_storage`, `set_storage` and `access_storage`, so operands that
  differ only above bit 255 address the same slot and share its warm or cold
  status. The QRVMC host ABI (`qrvmc_bytes64` key) is unchanged. Resolves the
  qrvmone side of [theQRL/go-qrl#119](https://github.com/theQRL/go-qrl/issues/119);
  the new unit tests use the same bytecode as the go-qrl conformance vectors
  from [cyyber/go-qrl#214](https://github.com/cyyber/go-qrl/pull/214).
  The state test host warms transaction access-list storage keys under the
  same folded key.
- `CREATE2` address derivation in the state test host embeds the 32-byte
  keccak256 digest of the init code, as go-qrl's `crypto.CreateAddress2`
  does, instead of the 64-byte container holding it (Ciphsure QRVM-1).
  Expected addresses in `state_new_account_address_test.cpp` were recomputed
  with go-qrl, and go-qrl's own `TestNewContractAddress` vectors were added.
- The state and storage tries are keyed by the 32-byte keccak256 digest, as
  go-qrl's secure trie is, and the storage trie hashes the 32-byte storage
  key rather than its 64-byte container (Ciphsure QRVM-12). State roots now
  match go-qrl: the expected roots in `state_mpt_hash_test.cpp` were
  recomputed with go-qrl's trie package, and the post-state root of the
  statetest integration fixture is the one go-qrl's `qrvm statetest`
  reports for it.
- The state test transaction RLP matches go-qrl's `DynamicFeeTx` wire
  encoding: access-list storage keys are encoded as 32-byte strings, the
  transaction carries go-qrl's 3-byte `descriptor` and `extraParams` fields
  (read from JSON under those names, descriptor empty or 3 bytes), and the
  tail is `descriptor, extraParams, signature, publicKey`. Transaction hashes
  and `qrvmone-t8n`'s `txRoot` now agree with go-qrl; expected values in
  `state_rlp_test.cpp` and `state_mpt_hash_test.cpp` were recomputed with
  go-qrl's encoder, and vectors with ML-DSA-87 sized signature and public key
  were added.
- The state test loader requires addresses in JSON to be `Q` followed by
  exactly 128 hex characters, as go-qrl's `common.Address` parsing does,
  instead of left-padding shorter forms or reading an empty string as the
  zero address. An empty `to` still denotes contract creation. The
  statetest integration fixture
  and the benchmark fixtures under `test/qrvm-benchmarks` were rewritten with
  full-length addresses, so go-qrl's `qrvm statetest` loads the integration
  fixture unchanged and passes it.

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

[Unreleased]: https://github.com/theQRL/qrvmone/compare/v0.12.1...HEAD
[0.12.1]: https://github.com/theQRL/qrvmone/releases/tag/v0.12.1
[0.12.0]: https://github.com/theQRL/qrvmone/releases/tag/v0.12.0
[0.11.0]: https://github.com/theQRL/qrvmone/releases/tag/v0.11.0

[Keep a Changelog]: https://keepachangelog.com
[Semantic Versioning]: https://semver.org
