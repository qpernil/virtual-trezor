#!/usr/bin/env python3
"""Compile the firmware fork RNG sources and exercise OS entropy and failure paths."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

PROJECT = Path(__file__).resolve().parents[1]
UPSTREAM = PROJECT / "upstream/trezor-firmware"


def run(*args, **kwargs):
    return subprocess.run(*args, check=True, **kwargs)


with tempfile.TemporaryDirectory(prefix="virtual-trezor-rng-") as temporary:
    root = Path(temporary)
    paths = ["sys/rng/unix/rng.c", "sys/rng/stm32/rng_use_flags.c",
             "sys/rng/inc/sys/rng.h", "sys/rng/inc/sys/rng_mock.h",
             "sys/rng/inc/sys/rng_use_flags.h",
             "sec/optiga/unix/optiga_mock.c", "sec/tropic/unix/tropic_mock.c"]
    for name in paths:
        relative = Path("core/embed") / name
        dest = root / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(UPSTREAM / relative, dest)
    include = root / "include"
    (include / "sec").mkdir(parents=True)
    (include / "trezor_types.h").write_text("#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n")
    (include / "trezor_rtl.h").write_text('''#pragma once
#include <trezor_types.h>
#include <stdio.h>
#include <stdlib.h>
#define sectrue 1
#define ensure(condition, message) do { if ((condition) != sectrue) abort(); } while (0)
''')
    (include / "rand.h").write_text("void random_buffer(uint8_t*, size_t);\n")
    (include / "sec/optiga.h").write_text("bool optiga_random_buffer(uint8_t*, size_t);\n")
    (include / "sec/tropic.h").write_text("bool tropic_session_start(void);\nbool tropic_random_buffer(void*, size_t);\n")
    compiler = shlex.split(os.environ.get("CC", "cc"))
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-DTREZOR_EMULATOR", "-DSECURE_MODE",
             "-DVIRTUAL_TREZOR_OS_RNG", f"-I{include}", f"-I{root}/core/embed/sys/rng/inc"]
    objects = []
    for name in ["sys/rng/unix/rng.c", "sys/rng/stm32/rng_use_flags.c",
                 "sec/optiga/unix/optiga_mock.c", "sec/tropic/unix/tropic_mock.c"]:
        obj = root / (Path(name).stem + ".o")
        extra = ["-Dfopen=test_fopen", "-Dfread=test_fread"] if name.endswith("/rng.c") else []
        run(compiler + flags + extra + ["-c", str(root / "core/embed" / name), "-o", str(obj)])
        objects.append(str(obj))
    binary = root / "test_safe3_random"
    run(compiler + flags + [str(PROJECT / "tests/test_safe3_random.c"), *objects, "-o", str(binary)])
    run([str(binary)])
    symbols = subprocess.check_output(["nm", str(binary)], text=True)
    forbidden = {"rng_reseed", "rng_mock_reseed", "rng_mock_fill", "random_reseed",
                 "optiga_random_reseed", "tropic_random_reseed"}
    assert not any(line.split()[-1].lstrip("_") in forbidden for line in symbols.splitlines() if line.split())
print("Safe3 fork RNG: entropy paths, use flags, failure handling, and no reseeding passed")
