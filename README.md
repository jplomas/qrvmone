# qrvmone

[![release badge]][Releases]
[![license badge]][Apache License, Version 2.0]

> Fast Quantum Resistant Virtual Machine implementation

_qrvmone_ is a C++ implementation of the Quantum Resistant Virtual Machine (QRVM),
the smart contract execution engine of the [QRL] network. It is a fork of
[evmone], the Ethereum Virtual Machine implementation by the Ipsilon team,
adapted to the QRVM's 64-byte machine word and addresses. It exposes the [QRVMC]
API, so any QRVMC host can load it as an execution module, and it ships the
tooling used to check that it executes exactly like [go-qrl], the node that runs
the network.

## The QRVM in brief

The QRVM keeps the EVM's instruction set and gas schedule where it can, with these
differences:

- The machine word is 512 bits and addresses are 64 bytes (`qrvmc_bytes64` and
  `qrvmc_address` in QRVMC). `MLOAD` and `MSTORE` move 64-byte words.
- Storage values are 64 bytes. Storage keys are the low 256 bits of the slot
  operand, and hashes (`KECCAK256`, code hashes, trie keys) are 32 bytes, held
  in the low half of a 64-byte word. This matches go-qrl, which keys its state
  by 32-byte hashes.
- The opcode space makes room for `PUSH33` to `PUSH64`: `PUSH0` to `PUSH64` are
  `0x5f` to `0x9f`, `DUP1` to `DUP16` start at `0xa0`, `SWAP1` to `SWAP16` at
  `0xb0` and `LOG0` to `LOG4` at `0xc0`. There is no `SELFDESTRUCT`.
- There is a single revision, Zond (`QRVMC_ZOND`, revision 0).
- Addresses are written with a `Q` prefix in all tooling and test fixtures.

## Characteristics of qrvmone

1. Exposes the [QRVMC] API; the entry point is `qrvmc_create_qrvmone()` in
   `include/qrvmone/qrvmone.h`.
2. Requires a C++20 compiler.
3. Uses the [intx] library for 256-bit and 512-bit integer arithmetic and the
   [ethash] library for Keccak.
4. Contains two interpreters, selected with QRVMC options:
   - **Baseline** (default). A straightforward, efficient implementation that
     performs only minimal `JUMPDEST` analysis. The `cgoto=no` option turns off
     computed-goto dispatch.
   - **Advanced** (`advanced` option). Uses indirect call threading: a loaded
     program is a table of pointers to functions implementing the instructions.
     Gas cost and stack requirements of each basic block are precomputed and
     applied once per block, after an extensive bytecode analysis.

   The `trace` and `histogram` options enable execution tracing and an opcode
   histogram on either interpreter.

## Usage

### As a QRVMC module

qrvmone builds as a shared library (`libqrvmone.so`, `libqrvmone.dylib` or
`qrvmone.dll`) that any host using the QRVMC loader can open. The QRVMC command
line tool, built alongside the tests, runs bytecode through it directly:

```bash
build/bin/qrvmc --vm build/lib/libqrvmone.so run 600160020160005260406000f3
```

```
Executing on Zond with 1000000 gas limit

Result:   success
Gas used: 24
Output:   0000...0003
```

Two projects consume qrvmone this way:

- [hyperion], the Solidity compiler for the QRVM, builds qrvmone from source and
  loads it as the test engine for its test suite. Pinned versions live in
  hyperion's `test/qrvmc/README.md`.
- [go-qrl] compares its own Go implementation of the QRVM against qrvmone, vector
  by vector, with `qrvmone-conformance` (see [Conformance with go-qrl]).

go-qrl executes contracts with its built-in Go VM; it does not load qrvmone as a
plugin.

### Building from source

Requirements: a C++20 compiler (GCC, Clang or MSVC), CMake 3.18 or newer
(CMake 4 works), Git with submodules, and network access on the first configure,
when [Hunter] downloads intx, ethash and the test dependencies.

1. Fetch the source code:
   ```bash
   git clone --recursive https://github.com/theQRL/qrvmone
   cd qrvmone
   ```

2. Configure the build. `QRVMONE_TESTING=ON` also builds the tests and tools
   described below.
   ##### Linux / macOS
   ```bash
   cmake -S . -B build -DQRVMONE_TESTING=ON
   ```
   Add `-G Ninja` if Ninja is installed.

   ##### Windows
   ```bash
   cmake -S . -B build -DQRVMONE_TESTING=ON -G "Visual Studio 17 2022" -A x64
   ```

3. Build:
   ```bash
   cmake --build build --parallel
   ```

   The library lands in `build/lib` and the tools in `build/bin`.

Useful CMake options:

| Option | Default | Effect |
|---|---|---|
| `BUILD_SHARED_LIBS` | `ON` | Build qrvmone as a shared library. |
| `QRVMONE_TESTING` | `OFF` | Build the unit tests, state test tools and benchmarks. |
| `QRVMONE_FUZZING` | `OFF` | Instrument the library and build the fuzzer. |
| `QRVMONE_X86_64_ARCH_LEVEL` | `2` (`1` on macOS) | x86-64 micro-architecture level to compile for. |

### Testing

Run the unit tests, or everything registered with CTest, from the build
directory:

```bash
build/bin/qrvmone-unittests
ctest --test-dir build
```

`qrvmc-vmtester` checks that the built module meets the QRVMC contract:

```bash
build/bin/qrvmc-vmtester build/lib/libqrvmone.so
```

### Tools

All tools are built with `QRVMONE_TESTING=ON`.

| Tool | Purpose |
|---|---|
| `qrvmc` | QRVMC command line tool. `qrvmc --vm <module> run <code>` executes bytecode. |
| `qrvmc-vmtester` | QRVMC compliance tests for a VM module. |
| `qrvmone-unittests` | The unit test suite. |
| `qrvmone-statetest` | Runs state tests in JSON form: `qrvmone-statetest <file or directory>`. `--trace` prints an execution trace. |
| `qrvmone-t8n` | State transition tool with go-qrl's `t8n` style JSON input and output (`--input.alloc`, `--input.env`, `--input.txs`, `--output.*`, `--state.fork`, `--state.chainid`). |
| `qrvmone-bench` | Benchmarks: `qrvmone-bench test/qrvm-benchmarks/benchmarks`. |
| `qrvmone-conformance` | Runner for go-qrl's VM conformance corpus; see below. |

State test and t8n fixtures use go-qrl's conventions: `Q` followed by exactly
128 hex characters for addresses, 32-byte storage keys and 64-byte storage
values. go-qrl's `qrvm statetest` loads the integration fixture in
`test/integration/statetest` unchanged and passes it.

### Conformance with go-qrl

go-qrl keeps a corpus of VM conformance vectors in `core/vm/conformance` and a
test, `TestGoVMMatchesQrvmone`, that runs every vector through both its Go VM and
qrvmone and requires identical return data, error class and gas. The qrvmone side
is `qrvmone-conformance`, which reads vectors as tab separated lines on standard
input and prints one result per line. From a go-qrl checkout:

```bash
QRVMONE_DIR=/path/to/qrvmone hack/vm-conformance.sh
```

The script configures and builds `qrvmone-conformance` in `QRVMONE_DIR/build`
and runs the comparison.

## References

1. [Efficient gas calculation algorithm for QRVM](docs/efficient_gas_calculation_algorithm.md)
2. [QRVMC], the QRVM client and VM connector API.
3. [go-qrl], the Go implementation of the QRL protocol.
4. [hyperion], the Solidity compiler for the QRVM.
5. [evmone], the upstream project qrvmone is forked from.

## Maintainers

qrvmone is maintained by [The QRL] team. The upstream evmone was created by
Paweł Bylica and the Ipsilon team.

## License

[![license badge]][Apache License, Version 2.0]

Licensed under the [Apache License, Version 2.0].

[release badge]: https://img.shields.io/github/v/release/theQRL/qrvmone
[license badge]: https://img.shields.io/badge/license-Apache--2.0-blue.svg
[Releases]: https://github.com/theQRL/qrvmone/releases
[Apache License, Version 2.0]: LICENSE
[QRL]: https://www.theqrl.org
[The QRL]: https://github.com/theQRL
[QRVMC]: https://github.com/theQRL/qrvmc
[go-qrl]: https://github.com/theQRL/go-qrl
[hyperion]: https://github.com/theQRL/hyperion
[evmone]: https://github.com/ethereum/evmone
[intx]: https://github.com/chfast/intx
[ethash]: https://github.com/chfast/ethash
[Hunter]: https://github.com/cpp-pm/hunter
[Conformance with go-qrl]: #conformance-with-go-qrl
