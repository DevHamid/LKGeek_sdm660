# 📋 Kernel Changelog — LKGeek_sdm660 (wayne / SDM660)

## ✅ Current Status

| Component | Version | Status |
|---|---|---|
| Kernel | 4.19.325-LKGeek-perf-devhamid | ✅ |
| ReSukiSU | v4.2.0-rc1 (35061/2) | ✅ Root working (native su) |
| SuSFS | v2.3.0 | ✅ Working |
| NoMount | v2.0.0 (Built-in) | ✅ Active |

**Branch:** `build-susfs-nomount`

---

## 🔧 Change Log

### SuSFS 2.2.0 → 2.3.0
- Replaced `fs/susfs.c`, `susfs.h`, `susfs_def.h` with fresh v2.3.0
- Applied remaining hunks: `patch -p1 -N --fuzz=0 -r rejects.log`
  (⚠️ `-N` is critical — without it, `patch` will silently **reverse**
  already-applied 2.x code instead of skipping it. Always dry-run
  first if unsure.)
- Removed `susfs_sys_reboot()` call in `kernel/reboot.c` — function
  dropped upstream in 2.3.0, no replacement exists

### 🐛 Root `su` fix (the big one)
`CONFIG_KSU_SUSFS_SUS_SU` was left enabled (never explicitly set),
creating a **competing** root-provisioning path that broke `ksud`'s
normal daemon-based `su` deployment under SUSFS Inline Hook mode.

**Fix:** `# CONFIG_KSU_SUSFS_SUS_SU is not set` in defconfig.

This one line fixed months of "su not detected" issues. If root
breaks again after any future SUSFS update, **check this setting
first** before assuming it's a ReSukiSU bug.

### NoMount v2.0.0 integration
- Script: `curl .../nomount/refs/heads/dev/kernel/setup.sh | bash -`
- ⚠️ **Known gotcha:** the setup script clones NoMount as a nested
  git repo and symlinks `fs/nomount -> ../NoMount/kernel/src`. This
  works locally but **breaks on CI** (fresh clone doesn't resolve
  the symlink/embedded repo correctly → `can't open file
  "fs/nomount/Kconfig"`).
  **Fix applied:** stripped the embedded repo, copied real files
  directly into `fs/nomount/` instead of using the symlink.
- `CONFIG_NOMOUNT=y` added to defconfig

---

## 🔄 Future Upgrade Playbook

### Updating SuSFS
1. `git checkout <latest working tag> && git checkout -b update-attempt`
2. Check JackA1ltman's current branch name (has changed before:
   `mainline` → `sample`)
3. `rm fs/susfs.c include/linux/susfs.h include/linux/susfs_def.h`
4. Fetch new `susfs_patch_to_4.19.patch`
5. `patch -p1 -N --fuzz=0 -r rejects.log < patch_file`
6. Only manually fix files in `rejects.log` with genuinely **new**
   content — ignore "already applied"/skipped ones
7. Build → fix only real compile/link errors reported
8. **Test `su` still works** before calling it done (SUS_SU
   regression risk — see above)
9. Tag immediately once confirmed

### Updating ReSukiSU
- Driver is fetched fresh via `curl` at CI build time
  (`drivers/kernelsu/` is never committed to this repo) — updates
  automatically, nothing to do manually
- **After any ReSukiSU version bump, re-verify:**
  - `CONFIG_KSU_MANUAL_HOOK` vs `CONFIG_KSU_SUSFS` are still a
    Kconfig `choice` (mutually exclusive) — check
    `drivers/kernelsu/Kconfig` hasn't changed this structure
  - `CONFIG_KSU_SUSFS_SUS_SU` is still explicitly disabled
  - `su` still works natively post-flash

### Updating NoMount
1. Re-run the official `setup.sh` fresh
2. **Immediately check** `fs/nomount` isn't a symlink:
   `ls -la fs/nomount` — if it shows `->` (symlink), repeat the
   fix: delete symlink, copy real files from the cloned repo folder
   directly, delete the cloned repo folder, re-add to git
3. Confirm `git status` shows individual files, not a `160000`
   gitlink entry

---

## 🙏 Credits

- [Claude](https://claude.ai/share/e5a91d52-679d-4465-ad8f-2b1e72016f06) — making this project possible
- @wbprangga for prompt suggestion
- [Tashar02](https://github.com/Atom-X-Devs/scarlet_xiaomi_sdm660) for base kernel
- [Xiaolegun](https://github.com/xiaolegun) for base boot and wayne dev
- [@LKDenchin](https://github.com/LKDenchin) for base kernel
- [ReSukiSU](https://github.com/ReSukiSU/ReSukiSU) — driver & manager
- [JackA1ltman/NonGKI_Kernel_Build_2nd](https://github.com/JackA1ltman/NonGKI_Kernel_Build_2nd) — the non-GKI SUSFS backport patch and hook scripts that made this actually work
- maxsteeel (NoMount)
- [LKGeek-Team](https://github.com/LKGeek-Team) — base kernel
- SUSFS project — SuSFS v2.2.0
- [MI 6X (wayne) INDONESIA 🇮🇩](https://t.me/Mi6XGroup) for everything

---

*Built with way too much coffee and an unreasonable amount of `git checkout -f`.* ☕
