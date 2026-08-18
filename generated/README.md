# Generated code

Run the codegen (see `README.md` → "Building from source") after supplying your
legally obtained `.xex`. The files in this folder are **derived from the
copyrighted game executable** and are intentionally **excluded from version
control** — never commit them.

To regenerate:

```
cmake --build out/build/win-amd64-release --target dbz3_codegen
```

This produces `dbz3_init.*`, `dbz3_recomp.*`, `dbz3_register.cpp`,
`dbz3_register.h`, `sources.cmake`, etc. from `default.xex` +
`dbz3_config.toml` + `dbz3_manifest.toml`.
