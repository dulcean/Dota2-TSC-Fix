![logo](./assets/fix.png)

---

# dota2-tsc-fix

A workaround for seconds-long freezes in Dota 2 on Linux on machines where
**CPU 0's hardware timestamp counter lags behind the other CPUs**.

The script launches Dota in a separate systemd scope with a cpuset that excludes
CPU 0. Other applications retain their existing CPU allocation. BIOS settings,
boot parameters, the clocksource, and kernel security settings remain unchanged.

**This is a targeted workaround for confirmed TSC desynchronization, not a
universal FPS boost.** It may not help if your freezes have a different cause.

## When to use it

Typical symptoms:

- Periodic pauses lasting from a fraction of a second to several seconds despite otherwise normal FPS.
- During a pause, one `GlobPool/N` thread spins in `ThreadSpin` in
  `libtier0.so`, called from `libanimationsystem.so`.
- The main thread waits in `futex` or `epoll_wait`.
- The kernel log contains `TSC warp` and `Marking TSC unstable`.
- The longest pauses are close to the TSC offset between CPUs.

On the machine investigated, `ThreadSpin` reads the TSC directly using the
`RDTSC` instruction. When a thread migrates to a CPU whose counter lags behind,
a short wait can stretch into seconds. Switching the kernel clocksource to HPET
does not change what a userspace `RDTSC` instruction returns.

## Check before installing

```bash
journalctl -k -b | rg -i 'tsc|clocksource|warp'
cat /sys/devices/system/clocksource/clocksource0/current_clocksource
```

Using HPET alone does not prove there is a problem. To compare CPUs directly:

```bash
make
./build/check-tsc
```

The diagnostic does not require root. It temporarily changes only its own
process's CPU affinity, reads RDTSCP and CLOCK_MONOTONIC_RAW, and restores the
original affinity. Exit codes: `0` means the spread was below 1 ms in this short
measurement, `1` means the spread was at least 1 ms, and `2` indicates an error
or too few accessible CPUs.
The 1 ms threshold is a diagnostic heuristic, not a guarantee of healthy hardware.
With only two accessible CPUs, the offset reveals a difference but does not
identify which CPU is faulty. Run the check outside the restricted scope:
the excluded CPU will not be visible from inside it.

If a different CPU lags behind, excluding CPU 0 by default will not address it.
Specify the set of healthy CPUs using `DOTA_TSC_CPUS`.

## Requirements

- Linux, Bash, and systemd with a user session.
- A delegated cgroup v2 `cpuset` controller.
- For the optional diagnostic: an x86 CPU with RDTSCP, a C compiler, and make.

If the systemd user manager does not allow setting `AllowedCPUs`, the script
exits with an error. It does not fall back to launching the game without the
restriction. The script neither grants itself additional privileges nor changes
controller delegation.

## Installation and Steam setup

```bash
make install
```

Installation copies only the script to `~/.local/bin/dota2-tsc-fix`.

In Dota 2's properties in Steam, add the script's **absolute path** before `%command%`:

```text
/home/YOUR_USER/.local/bin/dota2-tsc-fix %command% -vulkan -novid
```

The script uses all online logical CPUs except CPU 0.
You can specify the CPU set explicitly if needed:

```text
DOTA_TSC_CPUS=1-11 /home/YOUR_USER/.local/bin/dota2-tsc-fix %command% -vulkan -novid
```

This example assumes a system with CPUs 0–11. Check your CPU range before
copying it: `cat /sys/devices/system/cpu/online`.
The selected set must exclude the affected CPU.

A normal `taskset` command sets CPU affinity, which the game can change.
A cpuset limits the maximum set of available CPUs even if the game calls
`sched_setaffinity` again.

## Verify after launching

While Dota is running:

```bash
dota_pid=$(pgrep -n -x dota2)
cat "/proc/$dota_pid/cgroup"
rg Cpus_allowed_list "/proc/$dota_pid/status"
```

With the default settings, CPU 0 should be absent from the allowed set.

## Results on the machine investigated

Ryzen 5 4600H, RTX 2060 Mobile, CachyOS, Linux 7.3.0-4-cachyos.
The kernel measured a TSC warp of 6,863,179,807 cycles at 2994.441 MHz:
**2.291973629 seconds**. A direct comparison showed that only CPU 0 lagged
behind, by approximately 2.291973 seconds.

| Metric | Before the workaround | After the workaround |
|---|---:|---:|
| Recording duration | 120 seconds | 92 seconds |
| Average FPS | 135.6 | 143.9 |
| Median frame time | 6.17 ms | 6.16 ms |
| 99.9th percentile | 83.8 ms | 23.7 ms |
| Longest pause | 2301 ms | 72 ms |
| Pauses longer than 100 ms | 15 | 0 |

## Limitations and rollback

- The workaround excludes one **logical CPU**, not an entire physical core.
  This reduces the computing resources available to the game.
- The offset may change after a reboot. Repeat the diagnostic after a BIOS
  update or a change in system behavior.
- This works around a platform issue; it does not repair the hardware timer.
- The script targets a regular Steam installation. Flatpak Steam has not been tested.
- If your TSC is synchronized, do not apply the workaround solely to increase FPS.

To roll back, remove the script's path and `DOTA_TSC_CPUS` from Steam's launch
options and restart the game. Systemd removes the scope after its processes exit.
You do not have to delete the installed script.


```bash
make check
```


```bash
./bin/dota2-tsc-fix python3 -c 'import os; os.sched_setaffinity(0, set(range(os.cpu_count()))); print(sorted(os.sched_getaffinity(0)))'
```
