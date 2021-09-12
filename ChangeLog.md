ChangeLog
=========

All notable changes to the project are documented in this file.

[v2.1.0][] - 2021-09-12
-----------------------

### Changes
- Sync changes wrt preference/distance in pimdd.conf with pimd sources
- Import `cfparse.y` from mrouted to replace homegrown .conf parser
  - Support for `default-route-distance` and `default-route-metric`
  - Support for `assert-timeout`
- Import kernel interface probing from mrouted
  - Fixes issues with discovering IP addresses on Linux/FreeBSD
  - Allows for multinetting, using `altnet` directive from pimd
- Align command line options with pimd
  - Rename options to match
  - Add `-n` and `-s` command line options
  - Add `-l LEVEL` to adjust log level
  - Add `-i IDENT` to assume another identity for .conf/.pid and syslog
  - Add `-u FILE` to override pimdd/pimctl IPC socket file
  - Add `-p FILE` to override pimdd PID file
  - Add `-w SEC` startup delay before interface probe
- Rip out unfinished SNMP support
- Add support for IGMPv3, querier and accepting membership reports
- Add support for `no phyint` to start with all interfaces disabled
- Add support for selecting `phyint` by name
- Add support for `pimctl`, from pimd.  The tool can be shared with
  pimd and used interchangeably between the two daemons
- Add support for PIM Generation ID (genid) in PIM Hello
- Add IP Router Alert option to IGMP and PIM messages, as per RFC2113
- Inform PIM neighbors when we go down/reconfigure, PIM Hello holdtime=0
- Trigger PIM Assert reelection when PIM neighbor goes down
- Add support for querying RPF metric from Linux kernel
- Drop periodic dump of vifs + mrt in log/stderr, we have `pimctl` now
- Check for root privileges *after* checking command line args
- Use configure'd paths for .conf, .pid, and .dump file
- Update license files, stripping "contact Kurt .." and similar, also
  update pimd and mrouted license changes, thank you OpenBSD!
- Cleanup, drop old pre-C89 `__P()` macro
- Import safe string API functions from OpenBSD
- Drop PIM-SM and DVMRP specific debug messages/levels, unused
- Add systemd unit file, enabled automatically at configure time
- Add man pages for pimd, pimctl, and pimd.conf in sections 8 and 5
- Support for building .deb packages for Debian and Ubuntu based systemd

### Fixes
- Fix PIM Assert handling, with new `assert-timeout` .conf option
- Fix `k_del_vif()`, does not work properly in Linux
- Fix `k_req_incoming()` on FreeBSD
- Fix 100% CPU usage, refactor linked list handling to use `queue.h`
- Fix IGMP querier election bug, lost querier "flag" on interface when
  PIM DR election was lost.  Thar draft spec PimDM is built around says
  largest IP wins DR election, but smallest wins IGMP, follow IGMP RFC
- Only stop active/enabled VIFs, prevent bogus error messages
- Fix unset DSCP value in PIM messages, Internet Control (DS6)
- Fix RSRR (optional) build errors
- Fix various memory leaks identified by Valgrind
- Fix various scary bug identified by Coverity Scan


[v0.2.1.0-beta1][] - 2020-05-16
-------------------------------

### Changes
- Converted to GNU Configure & Build system
- Use netlink on Linux, `SIOCGETRPF` doesn't exist
- Cleanup `#ifdef` hell in `accept_igmp()`, unreadable mess due to
  how various UNIX flavors handle `SOCK_RAW` and `IP_HDRINCL`
- Use modern signal API, which all UNIX dialects support
- Cleanup, rename reserved function name `log()` to `logit()`
- Relocate source files to `src/` subdirectory
- Use `strerror()` instead of old `sys_errlist[]` & C:o
- Import patches from FreeBSD ports collection:
  - https://www.freshports.org/net/pimdd/
  - https://svnweb.freebsd.org/ports/head/net/pimdd/

### Fixes
- Always copy `IFNAMSIZ` bytes interface name to kernel
- Fix missing break statement for SIGALARM in signal handler
- Fix `linux` vs `__linux__` ifdefs and drop a few SYSV ifdefs
- Fix signed vs unsigned comparisons across the tree
- Fix bit shift with negative value, fix imported from pimd, stems from
  FreeBSD PR https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=202959


[v0.2.1.0-alpha21][] - 1998-12-30
---------------------------------

Synch'ed with latest releave pimd (sparse) from USC:
- XXX Timer changes in defs.h compatible with entry/prune timeout changes?

### Changes
- (Almost) all timers manipulation now use macros
- pim.h and pim_var.h are in separate common directory
- added BSDI definition to pim_var.h (thanks to Hitoshi Adaeda)
- Now compiles under Linux (haven't check whether the PIMv2 kernel
  support in linux-2.1.103 works)
- allpimrouters deleted from igmp.c (already defined in pim.c)
- igmpmsg defined for IRIX 
- Linux patches to pim.c and defs.h, contributed by Jonathan Day
  <j.c.day@larc.nasa.gov>.  Notes from Jonathan:

> I'd like to report some bugs in the current version of PimD-Dense,
> which cause compilation to fail under Linux.  In `defs.h`, external
> variables that have already been defined are being re-defined, causing
> the compilation to fail.  In `pim.c`, there is a typing error -
> `if->ip_len` is referred to, as opposed to `ip->ip_len`.  I've
> included the changes I've been using to fix these bugs.

### Fixes
- Fixed entry timer initialization in `route.c:process_cache_miss()`
- BUG FIX: fix TIMEOUT definitions in difs.h (bug report by Nidhi Bhaskar)
  (originally, if timer value less than 5 seconds, it won't become 0)
- Fixed delay time for LAN prune scheduling to 4 seconds to comply with
  spec, which says that prunes should be delayed for time strictly
  greater than the maximum join delay of 3 seconds.
- Fixed Prune timeout behavior:
  - Prune timeout now triggers graft
  - If prune and entry timeout simultaneously, the prune is timed out
    first, triggering an entry timer refresh.
- Fixed Assert sending so that asserts sent from upstream routers
  without routes to the source send pref and metric of 0x7fffffff.


June 19, 1998
-------------

Modifications to comply with the latest PIM-DM spec revision:
- Graft retransmission period is now 3 seconds
- Random Join/Prune delay is 3 seconds

Some still unresolved issues:
- Upstream Address field in Graft and Graft-Ack


v0.2.1.0-alpha15 - May 29, 1998
-------------------------------

Synched in fixes from Pavlin's pimd-2.1.0-alpha15:

- Bug fixes related to NetBSD, thanks to Heiko W.Rupp <hwr@pilhuhn.de>
- `MRT_PIM` completely replaced by `MRT_ASSERT`


v0.2.1.0-alpha11 - May 29, 1998
-------------------------------

This is the first release of the the PIM Dense-Mode version of pimd.

For now, I have simply prepended "0." onto the version number for Pavlin
Ivanov Radoslavov's Sparse-Mode pimd, from which this code was derived.
A new 1.0.x-alpha version number will be assigned upon the initial
public release of this code.


[UNRELEASED]:     https://github.com/troglobit/pimd-dense/compare/2.1.0...HEAD
[v2.1.0]:         https://github.com/troglobit/pimd-dense/compare/0.2.1.0-beta1...2.1.0
[v0.2.1.0-beta1]: https://github.com/troglobit/pimd-dense/compare/0.2.1.0-alpha21...0.2.1.0-beta1
