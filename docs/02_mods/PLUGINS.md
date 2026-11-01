# Code plugins (DLL)

Supported way to load native code mods, such as accessibility mods, without wrapping the port's DLLs
(issue #23).

## For players
1. Create a folder named `plugins` next to `dbz3.exe` and put the plugin's `.dll` in it.
2. In the launcher's options, turn on **Code plugins**.
3. Restart the game.

The setting is **off by default**. Plugins are third-party code with the same access as the game, so only use
plugins you trust. Every plugin that is loaded, and every function it replaces, is written to `logs/dbz3_*.log`.

## For plugin authors
Copy [`src/rex_mod.h`](../../src/rex_mod.h) into your project. Export these with C linkage:

```c
__declspec(dllexport) void rex_mod_init(const RexModHost* host);  // required
__declspec(dllexport) void rex_mod_shutdown(void);               // optional
```

- `rex_mod_init` is called once:
  - after the game image is loaded and the region (`host->region`: `"us"` or `"eu"`) is detected;
  - before the game's first instruction runs.
- DLLs are loaded in alphabetical order.
- `rex_mod_shutdown` is called when the game closes normally.
- `host` stays valid for the whole session. Check `api_version` and `struct_size` before using fields added
  later; new fields are only ever appended.
- `guest_base + A` is guest address `A`. Guest memory is big-endian.
- `get_function(A)` returns the recompiled host function for guest address `A`, or NULL. To hook every call to a
  function, detour that pointer with your own hook library: recompiled code calls other functions directly.
- `set_function(A, f)` replaces the function-table entry for `A`. That table only serves indirect calls
  (function pointers and vtables).
- `log(plugin, message)` writes a line to the game log.
- Function addresses differ between the US and EU images. Use `host->region` to pick the right ones.

A minimal working example is in [`tests/plugin_prueba.cpp`](../../tests/plugin_prueba.cpp):

```
clang++ -shared -O1 -I src -o plugins/prueba.dll tests/plugin_prueba.cpp
```
