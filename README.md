# 🚀 LKGeek Wayne Kernel

### A carefully tuned Linux kernel for the Xiaomi Mi 6X

A community-built Linux 4.19.325 kernel for **Wayne / Mi 6X / SDM660**, focused
on smoother memory reclaim, responsive scheduling, practical root integration,
and useful networking and storage features.

This is the **`adios-wayne`** line: a feature-rich branch with the risky parts
kept visible, documented, and testable. It is built through GitHub Actions and
is intended for people who want to understand and tune their device, not just
flash an unexplained binary.

[![Kernel 4.19.325](https://img.shields.io/badge/kernel-4.19.325-2ea44f?style=flat-square&logo=linux&logoColor=white)](https://github.com/DevHamid/LKGeek_sdm660)
[![Wayne / SDM660](https://img.shields.io/badge/device-Wayne%20%2F%20SDM660-6f42c1?style=flat-square)](https://github.com/DevHamid/LKGeek_sdm660)
[![Build](https://img.shields.io/badge/build-GitHub%20Actions-2088ff?style=flat-square&logo=githubactions&logoColor=white)](https://github.com/DevHamid/Kernel_Action)
[![License GPL-2.0](https://img.shields.io/badge/license-GPL--2.0-lightgrey?style=flat-square)](LICENSES/preferred/GPL-2.0)

## ✨ Why This Kernel Exists

The Mi 6X is still capable hardware, but a modern Android workload asks a lot
from a small memory budget and an aging 4.19 base. This branch brings selected
memory, scheduler, storage, networking, and device-control improvements into
one Wayne-focused tree, while keeping the trade-offs honest.

### 🧠 Memory that reacts to the workload

**MGLRU v7 reclaim improvements**, re-swappiness, Le9uo protection logic,
ratio-based memory protection, and `kcompressd` work together in the reclaim
path. The goal is better control over which memory is protected, aged, or
reclaimed when the phone is under pressure.

### ⚡ Scheduling built for everyday interaction

**BORE**, **REFLEX**, and the adapted **Cambyses load-balancer behavior** target
different parts of responsiveness: bursty foreground work, CPU frequency
selection, and load migration. ADIOS is selected as the default MQ I/O
scheduler so storage scheduling is part of the same tuning story.

### 🛡️ Root and privacy integration

**BakaSU**, **SUSFS**, **NoMount**, and **Re-Kernel** are integrated for users
who need a controllable non-GKI kernel. The design keeps `SUS_SU` disabled for
the inline-hook setup and keeps the Binder hooks documented instead of hiding
them behind a vague "all features" label.

### 🌐 Modern networking on an old base

The tree includes **BBRv3**, a 4.19-adapted WireGuard port, and ARM64 BPF JIT fixes.
BBRv3 is now selected as the default for the Wayne build; CUBIC remains
compiled as a fallback. Device-specific throughput and latency still need testing.

### 📦 A practical toolbox

NTSYNC, ZRAM-IR, LZ4, EROFS, F2FS compression, Incremental FS, modem boot
control, LED triggers, KProfiles, and sound controls cover the features that
matter when the kernel is used as a daily phone rather than a benchmark image.

> **Honest status:** source presence, a successful CI build, and device
> validation are different things. This README marks those differences instead
> of turning every compiled feature into a performance promise.

## 🧭 Feature Map

| Area | What it brings | Current state |
|---|---|---|
| Memory | MGLRU v7 reclaim layer, re-swappiness, Le9uo, ratios, kcompressd, ZRAM-IR | Enabled in the verified build |
| Responsiveness | BORE, REFLEX, Cambyses LB_STRICT_BUDGET, ADIOS | Enabled; POC stays disabled on asymmetric Wayne CPUs |
| Root and control | BakaSU, SUSFS v2.4.0, NoMount, Re-Kernel | Enabled; some userspace behavior still needs device testing |
| Networking | BBRv3, WireGuard, BPF JIT fixes | BBRv3 selected and CI-verified; WireGuard portable stable fixes backported |
| Compatibility | NTSYNC and Droidspaces-oriented kernel prerequisites | NTSYNC enabled; full Wine/container support is unverified |
| Diagnostics | pstore and console-ramoops support | Enabled; warm-reboot persistence still needs confirmation |

## 🔍 Audit Snapshot

- Branch: `adios-wayne`
- Build source commit: `59de36f9f045d31e698e8cd25f1ec3c851fde74e`
- Build config: `arch/arm64/configs/vendor/wayne-oss-perf_defconfig`
- Verified CI: [Kernel_Action run 44772822339](https://github.com/DevHamid/Kernel_Action/actions/runs/44772822339)
- CI result: kernel, boot image, and AnyKernel3 artifacts built successfully

The status below separates source presence, resolved build configuration, and
device validation. `Unverified` means the source is present but the required
device or reboot test was not available during this audit.

## 🔧 Technical Feature Audit

### Original workflows.md checklist

| Feature | Source and build status | Result |
|---|---|---|
| MGLRU | 4.19 backport in `mm/`; active branch includes the v7 reclaim-loop layer; `CONFIG_LRU_GEN=y` and `CONFIG_LRU_GEN_ENABLED=y` | Implemented and enabled. The branch carries the v7 reclaim-loop changes (`170f950d9`, `0ff7ae760`, `0ffcea89e`, `a4e57b58a`) adapted to the 4.19 San-Kernel/v4-era base, plus local fixes. It is not a verbatim modern-kernel tree. Earlier reclaim tests booted and passed safety checks. |
| pstore / console-ramoops | Ramoops sizing, persistence, and diagnostics are present; Wayne reserves a 4 MiB region with record, console, and pmsg zones. `PSTORE`, `PSTORE_CONSOLE`, `PSTORE_PMSG`, `PSTORE_RAM`, and `PSTORE_LAST_KMSG` resolve on. | Implemented and enabled, but previous-boot persistence is unverified. A warm reboot must confirm `console-ramoops-0` contains the previous boot. |
| BakaSU | `KernelSU` points to Baka-SU commit `516d3e87fbc986ccefdde2c2765f1700612441a9`; CI reports `v4.2.0-rc3-516d3e87-dirty`. | Implemented, enabled, and CI-confirmed. `dirty` is the build-tree suffix, not a separate release. |
| SUSFS | `fs/susfs.c` and `include/linux/susfs.h`; source reports `v2.4.0`. | Implemented and enabled through inline hooks. `SUS_SU` is disabled. |
| Re-Kernel | Driver and Binder transaction/reply hooks are present. | Implemented and enabled with `CONFIG_REKERNEL=y`; userspace controller behavior remains device-dependent. |
| NoMount | Exact upstream v2.3.1 kernel sources under `fs/nomount/`; protocol version 23. | CI confirms `CONFIG_NOMOUNT=y` and `CONFIG_KEYS=y`; the driver compiled. The release recommends updating the separate metamodule alongside it. |
| POC selector | `kernel/sched/poc_selector.c` version `3.0.0` is integrated. | Implemented in source but disabled for Wayne. SDM660 has asymmetric CPU capacity and the code deliberately gates selection there. |
| REFLEX | `drivers/cpufreq/cpufreq_reflex.c` version `0.4.0`. | Implemented and enabled as the default CPU frequency governor. |
| BBRv3 | `net/ipv4/tcp_bbr.c` contains `BBR_VERSION 3`, `bw_hi`, and `inflight_hi`; Wayne defconfig selects `bbr`. | CI resolved config confirms `CONFIG_DEFAULT_TCP_CONG="bbr"`; CUBIC remains compiled. Comparative device performance is unmeasured. |
| LZ4 | Kernel LZ4 library reports `1.10.0`. | Implemented and enabled; ZRAM defaults to LZ4. |
| NTSYNC | `drivers/misc/ntsync.c` with 4.19 compatibility shims. | Implemented and enabled with `CONFIG_NTSYNC=y`; Wine/Proton runtime compatibility is untested. |
| Droidspaces | Namespace, cgroup, overlay, and networking prerequisites are staged. | Partial. `CONFIG_CGROUP_PIDS` and `CONFIG_CHECKPOINT_RESTORE` are off in the resolved image, so complete Droidspaces or Docker support is not proven. |
| LMK PSI | PSI code and memory-stall hooks exist. | Not enabled: `CONFIG_PSI` is off in the resolved Wayne image. Simple LMK is separate and enabled. |
| BORE | Scheduler code and Kconfig are present. | Implemented and enabled with `CONFIG_SCHED_BORE=y`; source history identifies BORE 3.1.8. |
| ADIOS | `block/adios.c` reports `ADIOS_VERSION "3.3.0"`. | Implemented, enabled, and selected as the default MQ I/O scheduler. Earlier freeze and flush-completion fixes are in the branch. |
| Cambyses | `SCHED_FEAT_LB_STRICT_BUDGET` is implemented in CFS. | Partial: only the LB_STRICT_BUDGET behavior from Cambyses v0.6.0 is present, not a complete upstream import. |

### Additional features found outside the original checklist

| Feature | Evidence and status |
|---|---|
| Kcompressd | Reclaim-side swap-compression worker in `mm/vmscan.c`; branch history contains build and page-lock/writeback fixes. |
| Ratio memory protection | `ANON_MIN_RATIO`, `CLEAN_LOW_RATIO`, and `CLEAN_MIN_RATIO` are wired into reclaim; resolved values are `15/0/15`. |
| Re-swappiness / Le9uo | Commit `900e12fc7` reshapes MGLRU state for per-type `max_seq`, walk state, and swappiness-aware reclaim. `mm/vmscan.c` contains the Le9uo protection path and swappiness values up to 200. | Present, compiled, and enabled as part of MGLRU. There is no separate `CONFIG_RE_SWAPPINESS` or `CONFIG_LE9UO` symbol; these are integrated MGLRU behaviors. |
| ZRAM-IR | Branch history contains the ZRAM-IR 1.3 immediate-recompression backport and idle-accounting fix; ZRAM, writeback, multi-compression, and LZ4 are enabled. |
| WireGuard upstream backport | `CONFIG_WIREGUARD=y`. The 4.19 port now carries the portable fixes from upstream stable HEAD `6d02c757940afc798e0eb294108262c3e55b1c5d`: unaligned-safe IPv6 allowed-IP handling and keypair zeroization on free. | Built successfully. The remaining upstream differences are 6.19+ API refactors (generated netlink/split ops, `sockaddr_inet`, `DEV_STATS`, `timer_delete`, `udp_tunnel` signatures) that do not apply to 4.19. A live VPN test is still required. |
| ARM64 BPF JIT fixes | Range-safe TBZ/TBNZ handling, JSET fallbacks, and related backported optimizations; BPF JIT is enabled. |
| Baseband Guard | `security/baseband-guard/` and SELinux build-order fixes; `CONFIG_BBG=y`. Boot and recovery blocking remain disabled. |
| Modem boot control | `drivers/soc/qcom/modem_boot.c` and Kconfig/Makefile integration; `CONFIG_QCOM_MODEM_BOOT=y`. Runtime behavior needs device testing. |
| LED and device controls | LED timer/oneshot/heartbeat and QTI tri-LED changes; `CONFIG_KPROFILES=y` and `CONFIG_SOUND_CONTROL=y`. |
| Filesystem support | EROFS, F2FS compression, and Incremental FS are enabled. |

## ⚙️ Resolved Configuration Notes

The source defconfig is not sufficient to describe the final kernel. Kernel_Action run 44772822339 built source commit `59de36f9f045d31e698e8cd25f1ec3c851fde74e`; its resolved config and published artifacts were verified.

- MGLRU is enabled by default, including the v7 reclaim-loop layer and re-swappiness/Le9uo structural support.
- ADIOS is the default MQ scheduler.
- BORE and REFLEX are enabled; REFLEX is the default CPU governor.
- Run 44772822339 resolved config confirms BBRv3 as the TCP default; CUBIC is still available.
- PSI is disabled.
- MEMCG and MEMCG_SWAP are enabled.
- MEMCG_KMEM is disabled by the branch's boot-stability fix.
- Simple LMK remains enabled alongside MEMCG.
- POC is disabled.
- NTSYNC, Re-Kernel, NoMount, SUSFS, WireGuard, ZRAM, and BPF JIT are enabled.
- `CONFIG_CGROUP_PIDS` and `CONFIG_CHECKPOINT_RESTORE` are disabled, so Droidspaces support is not a complete Docker claim.
- Kconfig emits warnings for KernelSU choice values and duplicate or invalid defconfig assignments. The build completed, but these warnings should be cleaned up before treating the configuration as final.

## 📝 Changelog for adios-wayne

### 2026-11-20

- Updated the NoMount kernel integration to the exact v2.3.1 upstream source; the cloud build confirms `CONFIG_NOMOUNT=y` and `CONFIG_KEYS=y`.
- Selected BBRv3 as the Wayne TCP default; the resolved config confirms `CONFIG_DEFAULT_TCP_CONG="bbr"`, with CUBIC retained as an available alternative.
- Backported the remaining portable WireGuard fixes from stable HEAD `6d02c757940afc798e0eb294108262c3e55b1c5d` (unaligned allowed-IP load, keypair wipe); newer upstream changes depend on 6.19+ APIs.
- Verified cloud build [44772822339](https://github.com/DevHamid/Kernel_Action/actions/runs/44772822339) on branch `wg-upstream-4.19`, then fast-forwarded `adios-wayne` to the same tree. Device testing remains outstanding.

### 2026-11-15

- Layered the adapted MGLRU v7 reclaim-loop changes onto the 4.19 base.
- Integrated the re-swappiness and Le9uo MGLRU structure changes; these are part of MGLRU rather than separate Kconfig features.

### 2026-11-16

- Enabled MEMCG in `wayne-oss-perf_defconfig`.
- Kept MEMCG_KMEM disabled because the 4.19 MGLRU tree hangs during early boot with slab accounting enabled.
- Removed the Simple LMK `!MEMCG` conflict so Simple LMK remains available with MEMCG.
- Added the slab compatibility stub for the MEMCG_KMEM-off configuration.

### 2026-11-12

- Added Qualcomm SDM660 modem boot control.
- Added ARM64 BPF JIT diagnostics and range-safe branch fallbacks.
- Applied the local WireGuard 4.19 API fixes.

### 2026-11-11

- Updated kernel-side SUSFS to v2.4.0.
- Added 4.19 compatibility wrappers for SUSFS.
- Made REFLEX v0.4.0 the default CPU frequency governor.

### 2026-11-09

- Backported ZRAM-IR 1.3 immediate recompression and corrected idle accounting.
- Added the NTSYNC 4.19 port and compatibility fixes.
- Added Cambyses LB_STRICT_BUDGET behavior.
- Fixed ramoops console sizing and previous-buffer preservation logic.
- Added ramoops zone-state diagnostics.

### 2026-11-08

- Ported BORE 3.1.8 and ADIOS 3.3.0 to the 4.19 scheduler and block layers.
- Added POC selector 3.0.0, then disabled it for Wayne because asymmetric CPU capacity makes it ineligible.
- Fixed 4.19 scheduler integration issues.
- Added Droidspaces-oriented non-GKI 4.19 configuration support.
- Switched KernelSU integration from ReSukiSU to the BakaSU submodule.

### Earlier integration work

- Added SUSFS v2.3.0, followed by the current v2.4.0 update.
- Added NoMount as flattened tracked files for clean CI checkouts.
- Added Re-Kernel driver integration and Binder transaction/reply hooks.
- Added Baseband Guard with the legacy 4.19 SELinux integration path.
- Added BBRv3, LZ4 1.10.0, and device-specific performance/control changes.

## 🧪 Remaining Validation and Cleanup

1. Capture a warm-reboot pstore result from the current image and inspect `console-ramoops-0`.
2. Run a real NTSYNC/Wine userspace test before claiming application compatibility.
3. Run a container test before claiming Droidspaces or Docker support.
4. Compare Wayne throughput and latency with BBRv3 against the CUBIC fallback under the same network and queue-discipline settings.
5. Enable PSI only in an isolated test build if LMK PSI is still desired.
6. Clean Kconfig warnings and invalid duplicate defconfig assignments.
7. Test NoMount behavior and compare BBRv3/CUBIC throughput and latency on Wayne; the CI build does not replace device validation.

## 🛠️ Build

Build through [Kernel_Action](https://github.com/DevHamid/Kernel_Action). The local tree is for source review and changes; full kernel compilation belongs in GitHub Actions.

## 🙏 Credits

- [LKGeek-Team](https://github.com/LKGeek-Team) and the original SDM660 contributors
- [Baka-SU/BakaSU](https://github.com/Baka-SU/BakaSU)
- [JackA1ltman/NonGKI_Kernel_Build_2nd](https://github.com/JackA1ltman/NonGKI_Kernel_Build_2nd)
- [Sakion-Team/Re-Kernel](https://github.com/Sakion-Team/Re-Kernel)
- [maxsteeel/nomount](https://github.com/maxsteeel/nomount)
- [firelzrd/adios](https://github.com/firelzrd/adios), [bore-scheduler](https://github.com/firelzrd/bore-scheduler), and [cambyses](https://github.com/firelzrd/cambyses)
- [google/bbr](https://github.com/google/bbr)
- [ravindu644/Droidspaces-OSS](https://github.com/ravindu644/Droidspaces-OSS)
