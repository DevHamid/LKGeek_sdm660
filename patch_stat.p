#!/usr/bin/env python3
import re

file_path = "fs/stat.c"
with open(file_path, "r") as f:
    content = f.read()

# newfstatat hook
pattern_newfstatat = r"(SYSCALL_DEFINE4\(newfstatat.*\)\s*\{)"
hook_newfstatat = r"""\1
#ifdef CONFIG_KSU_MANUAL_HOOK
    ksu_handle_stat(&dfd, &filename, &flag);
#endif
"""
content = re.sub(pattern_newfstatat, hook_newfstatat, content, count=1, flags=re.S)

# newfstat return hook
pattern_newfstat = r"(SYSCALL_DEFINE2\(newfstat.*\)\s*\{[^}]*return error;)"
hook_newfstat = r"""\1
#ifdef CONFIG_KSU_MANUAL_HOOK
    ksu_handle_newfstat_ret(&fd, &statbuf);
#endif
"""
content = re.sub(pattern_newfstat, hook_newfstat, content, count=1, flags=re.S)

# fstat64 return hook
pattern_fstat64 = r"(SYSCALL_DEFINE2\(fstat64.*\)\s*\{[^}]*return error;)"
hook_fstat64 = r"""\1
#ifdef CONFIG_KSU_MANUAL_HOOK
    ksu_handle_fstat64_ret(&fd, &statbuf);
#endif
"""
content = re.sub(pattern_fstat64, hook_fstat64, content, count=1, flags=re.S)

with open(file_path, "w") as f:
    f.write(content)

print("✅ fs/stat.c patched with stat hooks")
