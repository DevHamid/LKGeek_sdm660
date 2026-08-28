#!/usr/bin/env python3
import re

file_path = "fs/open.c"

# Read the file
with open(file_path, "r") as f:
    content = f.read()

# Insert extern declaration after do_faccessat definition
pattern_decl = r"(long do_faccessat\(.*\)\s*\{[^}]*\})"
decl_code = r"""\1

#ifdef CONFIG_KSU_MANUAL_HOOK
__attribute__((hot)) 
extern int ksu_handle_faccessat(int *dfd, const char __user **filename_user,
                                int *mode, int *flags);
#endif
"""
content = re.sub(pattern_decl, decl_code, content, count=1, flags=re.S)

# Insert hook call inside SYSCALL_DEFINE3(faccessat)
pattern_call = r"(SYSCALL_DEFINE3\(faccessat.*\)\s*\{)"
call_code = r"""\1
#ifdef CONFIG_KSU_MANUAL_HOOK
    ksu_handle_faccessat(&dfd, &filename, &mode, NULL);
#endif
"""
content = re.sub(pattern_call, call_code, content, count=1, flags=re.S)

# Write back the modified file
with open(file_path, "w") as f:
    f.write(content)

print("✅ fs/open.c patched with faccessat hook")
