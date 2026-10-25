# CSV frame profiler (local fork)

Meson option `-Dprofile=true`. Use a **debugoptimized** build so timers and symbols stay on (not silent release).

Windows (MINGW64 on PATH):

```bat
set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%
cd /d C:\Dev\Rance Quest_RModding\RQ_modding_xsystem4
meson setup build-profile --buildtype=debugoptimized -Dprofile=true
ninja -C build-profile
```

Output: `xsystem4-profile.csv` next to the exe (override with env `XSYS4_PROFILE`). F12 pauses capture.

Do not push this branch until Moru asks.
