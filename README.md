# 🔥 LKGeek_sdm660 — ReSukiSU + SUSFS v2.2.0 Integration

**Device:** Xiaomi Mi 6X (`wayne`, SDM660)
**Kernel:** `4.19.325-LKGeek-perf-devhamid` (LKGeek-perf fork, non-GKI)
**Repo:** [DevHamid/LKGeek_sdm660](https://github.com/DevHamid/LKGeek_sdm660)
**Branch:** `susfs-inline-proper`

---

## ✅ Status: WORKING

| Component | Version | Status |
|---|---|---|
| ReSukiSU driver | v4.2.0-rc1-a9216b04 (35061/2) | ✅ Working |
| ReSukiSU Manager | v4.2.0-rc1 (35108/2) | ✅ Detects driver |
| SUSFS | v2.2.0 | ✅ Fully functional |
| Hook type | **Inline** (SUSFS-authored) | ✅ |
| Root | SuperUser: 2, Modules: 5 | ✅ |

---

## 🧩 The Core Problem (and the real fix)

For most of this project, root worked *or* SUSFS worked — never both. The root cause turned out to be a single, easily-missed detail in `drivers/kernelsu/Kconfig`:

> ReSukiSU exposes **three mutually exclusive hooking methods** as a Kconfig `choice`: `KSU_TRACEPOINT_HOOK`, `KSU_MANUAL_HOOK`, and `KSU_SUSFS` ("SUSFS Inline Hook").

Critically, **`KSU_SUSFS` is not "SUSFS features on/off."** Per ReSukiSU's own docs:

> *"SuSFS Inline Hook — An hook from SuSFS, like Manual Hook, but provided from **SuSFS project**, not this project."*

It's SUSFS's own independent implementation of the same hooking concept as Manual Hook — authored and maintained separately, using entirely different patch code. They are **alternatives, not layers.**

JackA1ltman's [NonGKI_Kernel_Build_2nd](https://github.com/JackA1ltman/NonGKI_Kernel_Build_2nd) wiki confirms this explicitly: two separate scripts exist —
- `syscall_hook_patches.sh` → for **KernelSU without SuSFS** (`CONFIG_KSU_MANUAL_HOOK=y`)
- `susfs_inline_hook_patches.sh` → for **KernelSU with SuSFS**, used **instead of** the above (`CONFIG_KSU_SUSFS=y`, no Manual Hook)

We had spent significant effort hand-patching `ksu_handle_*` calls (the Manual Hook style) into the source while *also* trying to keep SUSFS active — which is why detection kept silently failing. The fix was to strip all Manual-Hook-style patches back out and apply the **actual, authoritative `susfs_inline_hook_patches.sh`** script instead.

---

## 🔧 What Was Actually Done

1. **Diagnosed the Kconfig choice-group conflict** using a local `make defconfig` dry run (Termux, no full compile needed) — revealed `CONFIG_KSU_SUSFS` was silently overriding `CONFIG_KSU_MANUAL_HOOK` in the resolved `.config`, despite both being set `=y` in the raw defconfig text.
2. **Cleaned all 7 files** of the old Manual-Hook-style `ksu_handle_*` patches: `fs/exec.c`, `fs/open.c`, `fs/read_write.c`, `fs/stat.c`, `kernel/reboot.c`, `kernel/sys.c`, `drivers/input/input.c`.
3. **Fetched and ran the real `susfs_inline_hook_patches.sh`** from JackA1ltman's repo against the cleaned tree — patched all 12 target files correctly (some legitimately skipped per kernel-version checks baked into the script itself).
4. **Fixed one formatting edge case**: `drivers/input/input.c`'s `input_handle_event` function has its return type and name on separate lines (`static void\ninput_handle_event(...)`), which didn't match the script's single-line `sed` pattern — added the missing `extern` declaration manually to match.
5. **Set defconfig correctly for this path**: `CONFIG_KSU_SUSFS=y` + all `CONFIG_KSU_SUSFS_*` sub-options. **No `CONFIG_KSU_MANUAL_HOOK`.**
6. Confirmed SUSFS's own `fs/susfs.c` / `include/linux/susfs.h` / `susfs_def.h` (from the earlier JackA1ltman non-GKI backport patch) were already correctly in place — no need to reapply.

---

## ⚠️ Notes for Future-Me (upstreaming / next driver bump)

- **The CI workflow (`build-kernel.yml`) force-resets `drivers/input/input.c` and `fs/read_write.c`** via `git checkout -f` before re-fetching ReSukiSU fresh via `curl`. If future edits to these two files disappear mysteriously after a build, check this step first.
- **`drivers/kernelsu/` is fetched fresh from ReSukiSU's `main` branch on every CI build** — it is never committed to this repo. If ReSukiSU changes their Kconfig structure, function names, or hook signatures upstream, this integration may break silently. Re-verify the choice-group structure in `drivers/kernelsu/Kconfig` after any ReSukiSU version bump.
- When upgrading SUSFS or ReSukiSU versions in the future: **do not mix `syscall_hook_patches.sh` and `susfs_inline_hook_patches.sh`.** Pick one path per the table above, and re-run the *correct* corresponding script fresh rather than hand-patching.
- Tags left as checkpoints in this repo:
  - `working-manual-hook-root` — root-only, no SUSFS (Manual Hook path)
  - `working-susfs-inline-full` — root **and** SUSFS working together (current, correct state)

---

## 🙏 Credits

- [Claude](https://claude.ai) — making this project possible
- @wbprangga for prompt suggestion
- @LKDenchin for base kernel
- [ReSukiSU](https://github.com/ReSukiSU/ReSukiSU) — driver & manager
- [JackA1ltman/NonGKI_Kernel_Build_2nd](https://github.com/JackA1ltman/NonGKI_Kernel_Build_2nd) — the non-GKI SUSFS backport patch and hook scripts that made this actually work
- [LKGeek-Team](https://github.com/LKGeek-Team) — base kernel
- SUSFS project — SuSFS v2.2.0

---

*Built with way too much coffee and an unreasonable amount of `git checkout -f`.* ☕
