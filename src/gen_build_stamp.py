# Compile-time stamp in America/Sao_Paulo, DD/MM/YYYY HH:MM.
# Regenerated every ninja (build_always_stale). Not the launch clock.
import sys
from datetime import datetime, timezone, timedelta

try:
    from zoneinfo import ZoneInfo
    now = datetime.now(ZoneInfo("America/Sao_Paulo"))
except Exception:
    now = datetime.now(timezone(timedelta(hours=-3)))

stamp = now.strftime("%d/%m/%Y %H:%M")
out = sys.argv[1]
with open(out, "w", encoding="utf-8", newline="\n") as f:
    f.write("/* generated at compile — do not edit */\n")
    f.write("#include \"modloader.h\"\n\n")
    f.write("const char *rq_build_stamp(void)\n{\n")
    f.write("\treturn \"%s\";\n" % stamp)
    f.write("}\n")
