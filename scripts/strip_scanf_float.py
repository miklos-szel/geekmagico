"""Drop the framework's forced float-capable scanf from the link.

The ESP8266 Arduino builder hard-codes ``"-u", "_scanf_float"`` into LINKFLAGS
(framework-arduinoespressif8266/tools/platformio-build.py). That force-links newlib's
float-capable scanf family (~2.4KB of .irom0.text) whether or not anything scans a float.

Nothing in this firmware scans a float -- there is no scanf call at all -- so the flag is
pure cost against the OTA size ceiling documented in CLAUDE.md.

``build_unflags`` cannot do this safely: PlatformIO removes the first matching token, which
would strip the ``-u`` belonging to ``-u app_entry`` and break the link. So remove the
``-u``/``_scanf_float`` pair positionally instead.

``-u _printf_float`` is deliberately left alone: src/main.cpp formats "%.1f".

Registered as a ``post:`` script -- LINKFLAGS does not exist yet at ``pre:`` time.
"""

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

flags = list(env["LINKFLAGS"])

try:
    index = flags.index("_scanf_float")
except ValueError:
    # Framework changed how it forces this symbol. Fail loudly rather than silently
    # shipping a firmware that is 2.4KB larger than the size budget assumes.
    raise SystemExit(
        "strip_scanf_float.py: '_scanf_float' not found in LINKFLAGS. The ESP8266 "
        "framework build script changed -- re-check the flag and update this script."
    )

if index == 0 or flags[index - 1] != "-u":
    raise SystemExit(
        "strip_scanf_float.py: '_scanf_float' is not preceded by '-u' as expected; "
        "refusing to edit LINKFLAGS blindly."
    )

del flags[index - 1 : index + 1]
env.Replace(LINKFLAGS=flags)
