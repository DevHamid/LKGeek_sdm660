# 📋 Kernel Changelog & Documentation — LKGeek_sdm660 (wayne / SDM660)

## ✅ Current Status

| Component | Version | Status |
|---|---|---|
| Kernel | 4.19.325-LKGeek-perf-devhamid | ✅ Built & Booting |
| ReSukiSU | v4.2.0-rc1 (35061/2) | ✅ Root working (native `su`) |
| SuSFS | v2.3.0 | ✅ Active |
| NoMount | v2.0.0 (Built-in) | ✅ Active |
| Baseband-guard | v1.0.0 (Legacy LSM) | ✅ Active (Partitions Protected) |
| Re-Kernel | Sakion-Team (Binder Hooked) | ✅ Active (Freezer Engine) |

**Active Line:** `build-all-rekernel`

---

## 🗂️ Branch & Tag Registry

### Tags (Checkpoints — always build or branch from these):

| Tag | State / Components Included |
|---|---|
| `working-manual-hook-root` | Manual Hook baseline, root working, NO SuSFS |
| `working-susfs-2.2.0-su-fixed` | Root + SuSFS 2.2.0 + native `su` fix (`CONFIG_KSU_SUSFS_SUS_SU=n`) |
| `working-susfs-inline-full` | SuSFS 2.2.0 full working (pre-NoMount) |
| `working-bbg-nomount-susfs` | SuSFS 2.3.0 + NoMount v2.0.0 + Baseband-guard |
| `working-all-features` | 🌟 **Golden State:** All 5 components fully integrated & hooked |

### Branches:

| Branch | State / Purpose |
|---|---|
| `build-susfs-v2` | ❌ Abandoned — old Manual Hook conflict dead-end |
| `susfs-2.3.0-upgrade` | SuSFS 2.3.0 core upgrade sandbox |
| `build-susfs-nomount` | SuSFS 2.3.0 + NoMount v2.0.0 + Baseband-guard |
| `build-all-rekernel` | 🚀 **Current Active Base** — All features integrated (Re-Kernel hooked) |

---

## 🔧 Change Log

### 1. SuSFS 2.2.0 → 2.3.0 Upgrade
- Replaced `fs/susfs.c`, `include/linux/susfs.h`, `include/linux/susfs_def.h` with fresh v2.3.0 sources from JackA1ltman's `sample` branch.
- Applied remaining hunks with `patch -p1 -N --fuzz=0 -r rejects.log` (⚠️ `-N` prevents `patch` from silently reversing already-applied code).
- Dropped dead `susfs_sys_reboot()` call in `kernel/reboot.c` (upstream dropped reboot-spoofing in 2.3.0).

### 2. 🐛 Root `su` Native Fix (The Big One)
`CONFIG_KSU_SUSFS_SUS_SU` was defaulting to enabled, establishing a secondary/competing root interface that prevented `ksud` daemon from deploying standard `su` symlinks under Inline Hook mode.

- **Fix:** Explicitly set `# CONFIG_KSU_SUSFS_SUS_SU is not set` in defconfig.
- `su` now works completely natively without any userspace wrapper scripts or hacks.
- ⚠️ **Cleanup:** Any temporary `/system/bin/su` wrapper script calling `ksud debug su` in `anykernel.sh` is completely removed, ensuring clean, non-flaggable native root.

### 3. NoMount v2.0.0 Integration (Built-in)
- Integrated into kernel tree under `fs/nomount/`.
- ⚠️ **CI Symlink Gotcha:** Official script clones as an embedded git repository and symlinks `fs/nomount -> ../NoMount/kernel/src`, which breaks on clean CI checkouts. Flattened by placing actual files directly in `fs/nomount/`.
- Added `CONFIG_NOMOUNT=y` to defconfig.

### 4. Baseband-guard (BBG) LSM Integration
- Integrated `vc-teahouse/Baseband-guard` to prevent malicious/accidental raw block writes to bootloader, modem, and EFS partitions.
- **Non-GKI 4.19 Fixes:**
  - Bypassed strict `CONFIG_LSM` abort by setting `HAS_DEFINE_LSM := false` in BBG Makefile to force the built-in 4.19 legacy SELinux integration path.
  - Neutralized BBG's internal `flask.h` generation rule that wrote broken stubs and poisoned SELinux headers.
  - Enforced Kbuild directory build ordering in `security/Makefile`:
    ```makefile
    $(obj)/baseband-guard: $(obj)/selinux
    ```
    *(Note: must NOT have trailing slashes, or Kbuild ignores the rule).*
  - Mapped `security_initcall` to `late_initcall` in `security/baseband-guard/kernel_compat.h`.
  - Injected `struct bbg_cred_security_struct bbg_cred;` into SELinux's `struct task_security_struct` in `security/selinux/include/objsec.h`.

### 5. Re-Kernel (Sakion-Team Binder Freezer Hooks)
- Integrated kernel-level process freezer notification engine to eliminate notification delays and audio stutters when using tombstone managers (NoActive, Thanox, Scene).
- Built-in driver enabled via `CONFIG_REKERNEL=y` in `drivers/net/rekernel/`.
- **C-Level Binder Hooking:** Bypassed the Java modifier tool in favor of direct C injection in `drivers/android/binder.c`:
  - `REPLY` hook injected inside `binder_transaction()` in the `if (reply)` branch after `target_proc->tmp_ref++`.
  - `TRANSACTION` hook injected inside `binder_transaction()` in the `else` branch right before `security_binder_transaction()`.
- Note: Requires a companion app (NoActive recommended) to send freeze/thaw commands; driver stays idle when no controller is present.

---

## 🔄 Future Upgrade Playbook

### Updating SuSFS
1. `git checkout <latest working tag> && git checkout -b update-attempt`
2. Check JackA1ltman's current branch (switches between `mainline` and `sample`).
3. Delete pure SuSFS files: `rm -f fs/susfs.c include/linux/susfs.h include/linux/susfs_def.h`.
4. Fetch new `susfs_patch_to_4.19.patch`.
5. Apply with strict forward mode: `patch -p1 -N --fuzz=0 -r rejects.log < susfs_patch_to_4.19.patch`.
6. Verify `# CONFIG_KSU_SUSFS_SUS_SU is not set` is still preserved in defconfig!

### Updating Baseband-guard
1. When updating `security/baseband-guard/`:
   - Keep `HAS_DEFINE_LSM := false` in its Makefile.
   - Do NOT allow BBG Makefile to generate `flask.h` (let SELinux generate it).
   - Ensure `$(obj)/baseband-guard: $(obj)/selinux` remains in `security/Makefile` (no trailing slashes).
   - Ensure `security_initcall` is mapped to `late_initcall` in `kernel_compat.h`.
   - If resetting git files, remember to restore `bbg_cred` in `security/selinux/include/objsec.h`.

### Updating NoMount
1. Never commit `NoMount` as a submodule or symlink.
2. Copy files directly to `fs/nomount/` as plain tracked files.

### Updating Re-Kernel
1. If `drivers/android/binder.c` is ever reset, re-inject the two `rekernel_report()` calls inside `binder_transaction()` (both `REPLY` and `TRANSACTION` branches).
2. Ensure `CONFIG_REKERNEL=y` is set in defconfig.

---

## 📦 Distribution Note

Out of respect to **LKDenchin** and the **LKGeek Team**, who built the original base kernel this project stands on, **no prebuilt binaries are published for this fork.** If you want to use these changes, **fork this repository and build it yourself** using the included GitHub Actions workflow. Please keep all credit intact — do not strip attribution when forking or redistributing source.

---

## 🤖 Professional Handoff Prompt Template

*Use this prompt when handing context off to an AI assistant in future debugging or upgrading sessions to save tokens and prevent context loss:*

```text
CONTEXT: Xiaomi Wayne (Mi 6X / SDM660), Linux 4.19.325 custom kernel (fork: DevHamid/LKGeek_sdm660).
ACTIVE INTEGRATIONS & ARCHITECTURE:
1. ReSukiSU v4.2.0-rc1: Uses "CONFIG_KSU_SUSFS=y" (Inline Hook mode).
   - CRITICAL: "CONFIG_KSU_SUSFS_SUS_SU" MUST BE DISABLED in defconfig, or native su daemon fails to deploy.
2. SuSFS v2.3.0: Manually backported via JackA1ltman NonGKI_Kernel_Build_2nd.
3. NoMount v2.0.0: Built-in under fs/nomount/ (plain tracked files, no symlinks/submodules due to CI clone issues).
4. Baseband-guard (BBG): Running legacy SELinux integration.
   - HAS_DEFINE_LSM forced to false in BBG Makefile.
   - security/Makefile has explicit directory order: `$(obj)/baseband-guard: $(obj)/selinux` (no trailing slashes).
   - bbg_cred hooked into security/selinux/include/objsec.h.
   - security_initcall mapped to late_initcall in kernel_compat.h.
5. Re-Kernel: Driver in drivers/net/rekernel/ (CONFIG_REKERNEL=y) with IPC/Reply hooks injected in drivers/android/binder.c.

CONSTRAINT: Do not alter existing working hook placements without explicit verification against git diff. Be concise and provide copy-paste shell blocks.
```

---

## 🙏 Credits

- [Claude](https://claude.ai) & [Gemini](https://gemini.google.com) — architectural diagnosis, preprocessor debugging, and Kbuild ordering
- @wbprangga — prompt ideas & project inspiration
- [Tashar02](https://github.com/Atom-X-Devs/scarlet_xiaomi_sdm660) & [LKGeek-Team](https://github.com/LKGeek-Team) — base SDM660 kernel
- [Xiaolegun](https://github.com/xiaolegun) — Wayne boot & device development
- [@LKDenchin](https://github.com/LKDenchin) — kernel & CI maintainer
- [ReSukiSU](https://github.com/ReSukiSU/ReSukiSU) — kernel root engine & manager
- [JackA1ltman/NonGKI_Kernel_Build_2nd](https://github.com/JackA1ltman/NonGKI_Kernel_Build_2nd) — non-GKI SuSFS & Re-Kernel backport patches
- [maxsteeel (NoMount)](https://github.com/maxsteeel/nomount) — VFS module redirection
- [vc-teahouse (Baseband-guard)](https://github.com/vc-teahouse/Baseband-guard) — bootloader/modem partition defense
- [Sakion-Team (Re-Kernel)](https://github.com/Sakion-Team/Re-Kernel) — Binder process freezer
- [MI 6X (wayne) INDONESIA 🇮🇩](https://t.me/Mi6XGroup) — community testing & support

---

*Built with way too much coffee, zero sleep, and an unreasonable amount of `git checkout -f`.* ☕
```