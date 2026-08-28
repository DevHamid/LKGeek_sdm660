#!/usr/bin/env python3
import re

file_path = "fs/exec.c"
with open(file_path, "r") as f:
    content = f.read()

# do_execveat_common hook
pattern_execveat = r"(static int do_execveat_common.*\{)"
hook_execveat = r"""\1
#ifdef CONFIG_KSU_MANUAL_HOOK
    ksu_handle_execveat(&fd, &filename, &argv, &envp, &flags);
#endif
"""
content = re.sub(pattern_execveat, hook_execveat, content, count=1, flags=re.S)

# do_execve hook (if present)
pattern_execve = r"(int do_execve\(.*\)\s*\{)"
hook_execve = r"""\1
#ifdef CONFIG_KSU_MANUAL_HOOK
    ksu_handle_execve((int *)AT_FDCWD, filename, &argv, &envp, 0);
#endif
"""
content = re.sub(pattern_execve, hook_execve, content, count=1, flags=re.S)

with open(file_path, "w") as f:
    f.write(content)

print("✅ fs/exec.c patched with execve hooks")
