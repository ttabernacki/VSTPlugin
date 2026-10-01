#!/usr/bin/env python3
"""Fails if a Windows plug-in DLL imports the C/C++ runtime (i.e. needs the VC++ redistributable).

Usage: check_dll_imports.py path/to/OrbitPan.vst3   (needs: pip install pefile)
"""
import re
import sys

import pefile

BANNED = re.compile(r"^(vcruntime|msvcp|msvcr|vcomp|concrt|ucrtbase|api-ms-win-crt-)", re.I)

pe = pefile.PE(sys.argv[1])
imports = sorted({e.dll.decode() for e in pe.DIRECTORY_ENTRY_IMPORT})
print("imports:", ", ".join(imports))
bad = [i for i in imports if BANNED.match(i)]
exports = {e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
print("exports:", ", ".join(sorted(exports)))
if "GetPluginFactory" not in exports:
    sys.exit("FAIL: GetPluginFactory is not exported")
if bad:
    sys.exit("FAIL: depends on the C/C++ runtime DLLs: " + ", ".join(bad))
print("OK: no C/C++ runtime dependency")
