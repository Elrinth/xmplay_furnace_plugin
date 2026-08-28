# Third-party

Nothing here is vendored in git except `patches/`.

## Furnace (full headless engine)

`make` clones https://github.com/tildearrow/furnace at
`e14a0a3e2da06a5c7c63d4910be7f3759303f6f5` and applies:

1. `patches/furnace-engine-lib.patch` — `BUILD_ENGINE_LIB` static target,
   sndfile stubs, `cfile.cpp` include

There is **no** `furnace-dmf-strip.patch` and **no** `XMP_DMF_STRIP`.
All chip systems and the `.fur` / DefleMask / FamiTracker loaders stay in.

There is **no libopenmpt**.
