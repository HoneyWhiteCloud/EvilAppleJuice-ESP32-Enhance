"""PlatformIO post-script: redirect the linker map file to an ASCII-only path.

The pioarduino platform (used for ESP32-C6) appends
`-Wl,-Map="$BUILD_DIR/$PROGNAME.map"` to LINKFLAGS. The RISC-V linker (ld.exe)
cannot open a file whose path contains CJK characters, and this project lives in
a folder named "单片机". Redirecting the map file to the system temp directory
avoids that failure while keeping the rest of the build output inside the project.

This is loaded as a `post:` extra_script, so it runs after the framework build
script has already added the original `-Map` flag.
"""

Import("env")

import os
import tempfile

_progname = env.subst("$PROGNAME") or "firmware"
_map_path = os.path.join(tempfile.gettempdir(), _progname + ".map")


def _redirect(flags):
    fixed = []
    for flag in flags:
        if flag.startswith("-Wl,-Map"):
            fixed.append('-Wl,-Map="%s"' % _map_path)
        else:
            fixed.append(flag)
    return fixed


_linkflags = env.get("LINKFLAGS")
if _linkflags:
    env.Replace(LINKFLAGS=_redirect(list(_linkflags)))
