#!/usr/bin/env python3
import re

file_path = "kernel/sys.c"
with open(file_path, "r") as f:
    content = f.read()

pattern_setresuid = r"(long __sys_setresuid\(.*\)\s*\{)"
hook_setresuid = r"""\1
#ifdef CONFIG_KSU_MANUAL_HOOK
    (void)ksu_handle_setresuid(ruid, euid, suid);
#endif
"""
content = re.sub(pattern_setresuid, hook_setresuid, content, count=1, flags=re.S)

with open(file_path, "w") as f:
    f.write(content)

print("✅ kernel/sys.c patched with setresuid hook")
