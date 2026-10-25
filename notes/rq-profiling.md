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
- `xsystem4-profile-ain.log` — sleep/leftover events over ~50 ms with AIN stack

New CSV columns vs the first hub capture: `sleep_ms`, `n_sleep`, `sleep_req_ms`, `leftover_ms`, `leftover_other_ms`, `ain`.

F12 pauses capture. Do not push this branch until Moru asks.
