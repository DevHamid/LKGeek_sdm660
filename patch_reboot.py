#!/usr/bin/env python3
import re

file_path = "kernel/reboot.c"

# Read the file
with open(file_path, "r") as f:
    content = f.read()

# Insert extern declaration before SYSCALL_DEFINE4(reboot)
pattern_decl = r"(SYSCALL_DEFINE4\(reboot.*\)\s*\{)"
decl_code = r"""#ifdef CONFIG_KSU_MANUAL_HOOK
extern int ksu_handle_sys_reboot(int magic1, int magic2, unsigned int cmd, void __user **arg);
#endif

\1"""
content = re.sub(pattern_decl, decl_code, content, count=1, flags=re.S)

# Insert hook call inside SYSCALL_DEFINE4(reboot)
pattern_call = r"(SYSCALL_DEFINE4\(reboot.*\)\s*\{)"
call_code = r"""\1
#ifdef CONFIG_KSU_MANUAL_HOOK
    ksu_handle_sys_reboot(magic1, magic2, cmd, &arg);
#endif
"""
content = re.sub(pattern_call, call_code, content, count=1, flags=re.S)

# Write back the modified file
with open(file_path, "w") as f:
    f.write(content)

print("✅ kernel/reboot.c patched with sys_reboot hook")
