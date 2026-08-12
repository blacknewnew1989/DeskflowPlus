# RelayDesk File Transfer Starter

This directory is a small standalone Qt 6/C++20 scaffold for two high-risk primitives:

- incremental fixed-header frame decoding;
- cross-platform protocol path validation.

It is **not** the full product and is **not** assumed to match Deskflow target names. A6 should:

1. build and test it standalone;
2. compare style and utilities with the pinned upstream source;
3. fix any issue found;
4. move/adapt the code into the actual product module selected by A1;
5. keep the tests.

## Build

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On single-config generators, omitting `-C Release` is fine.

## Path scope

`PathPolicy::joinLexicallyUnderRoot` provides the P0 lexical containment used with the app-managed receive directory. Sender-side link entries are skipped. The first internal version deliberately does not add handle-by-handle path walking or enterprise security layers.

## License

The source files use the Deskflow-style GPL-2.0-only with the repository's LicenseRef OpenSSL exception identifier. A1 must verify the exact pinned-tag REUSE conventions when integrating.
