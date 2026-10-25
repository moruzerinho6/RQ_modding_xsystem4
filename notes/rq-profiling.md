# CSV frame profiler (local fork)

Meson option `-Dprofile=true`. Use a **debugoptimized** build so timers and symbols stay on (not silent release).

Windows (MINGW64 on PATH):

```bat
set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%
cd /d C:\Dev\Rance Quest_RModding\RQ_modding_xsystem4
meson setup build-profile --buildtype=debugoptimized -Dprofile=true
ninja -C build-profile
```

Output next to the exe (override with env `XSYS4_PROFILE`):

- `xsystem4-profile.csv` — one row per present
- `xsystem4-profile-ain.log` — sleep/leftover events over ~50 ms with AIN stack + top HLL
- `xsystem4-profile-hll.log` — per-HLL breakdown on leftover > 50 ms

Sleep columns kept. New vs the sleep capture: `script_ms`, `hll_ms`, `hll_other_ms`, `n_hll`, `chipmunk_sleep_ms`, `top_hll`, `top_hll_ms`.

F12 pauses capture. Do not push this branch until Moru asks.
