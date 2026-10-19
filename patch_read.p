#!/usr/bin/env python3
import re

file_path = "fs/read_write.c"
with open(file_path, "r") as f:
    content = f.read()

pattern_read = r"(SYSCALL_DEFINE3\(read.*\)\s*\{)"
hook_read = r"""\1
#ifdef CONFIG_KSU_MANUAL_HOOK
    if (unlikely(ksu_init_rc_hook))
        ksu_handle_sys_read(fd, &buf, &count);
#endif
"""
content = re.sub(pattern_read, hook_read, content, count=1, flags=re.S)

with open(file_path, "w") as f:
    f.write(content)

print("✅ fs/read_write.c patched with read hook")
