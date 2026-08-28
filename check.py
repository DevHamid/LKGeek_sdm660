import os
import re
import sys

def check_live_kernel():
    print("=" * 50)
    print("      LIVE PHONE KERNEL CHECK (Termux)      ")
    print("=" * 50)

    try:
        with open("/proc/version", "r") as f:
            print(f"📱 Kernel: {f.read().strip()}")
    except Exception as e:
        print(f"❌ Could not read kernel version: {e}")

    # Check OverlayFS
    try:
        with open("/proc/filesystems", "r") as f:
            if "overlay" in f.read():
                print("✅ PASS: OverlayFS driver registered in kernel")
            else:
                print("❌ FAIL: OverlayFS is NOT registered in kernel")
    except Exception as e:
        print(f"⚠️ Could not read /proc/filesystems: {e}")

    # Check kallsyms for KernelSU & SuSFS
    ksu_found = False
    susfs_found = False
    try:
        with open("/proc/kallsyms", "r") as f:
            for line in f:
                if "ksu_" in line or "kernelsu" in line:
                    ksu_found = True
                if "susfs_" in line:
                    susfs_found = True
                if ksu_found and susfs_found:
                    break
        
        if ksu_found:
            print("✅ PASS: KernelSU driver active in running kernel")
        else:
            print("❌ FAIL: KernelSU driver NOT detected in running kernel")

        if susfs_found:
            print("✅ PASS: SuSFS driver active in running kernel")
        else:
            print("⚠️ WARN: SuSFS driver NOT detected in running kernel")
    except PermissionError:
        print("⚠️ WARN: /proc/kallsyms is restricted by Android security.")
        print("   -> Run 'su -c python3 check.py' for full symbol access.")
    except Exception as e:
        print(f"⚠️ Could not check /proc/kallsyms: {e}")

def check_source_tree():
    hooks = [
        ("kernel/sys.c", r"ksu_handle_prctl", "prctl hook (kernel/sys.c)"),
        ("fs/exec.c", r"ksu_handle_execve", "execve hook (fs/exec.c)"),
        ("fs/open.c", r"ksu_handle_faccessat", "faccessat hook (fs/open.c)"),
        ("fs/stat.c", r"ksu_handle_stat|susfs", "stat/susfs hook (fs/stat.c)"),
        ("drivers/Makefile", r"kernelsu", "kernelsu entry (drivers/Makefile)"),
    ]

    has_source = any(os.path.exists(p[0]) for p in hooks)
    if not has_source:
        return

    print("\n" + "=" * 50)
    print("      LOCAL KERNEL SOURCE HOOKS CHECK       ")
    print("=" * 50)

    for rel_path, pattern, desc in hooks:
        if os.path.isfile(rel_path):
            with open(rel_path, "r", encoding="utf-8", errors="ignore") as f:
                if re.search(pattern, f.read()):
                    print(f"✅ PASS: {desc}")
                else:
                    print(f"❌ FAIL: Missing {desc}")

if __name__ == "__main__":
    check_live_kernel()
    check_source_tree()
