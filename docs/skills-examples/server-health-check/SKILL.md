---
name: server-health-check
description: Probe a remote host's services, disk, memory and certificates over SSH and report what is out of range. "health check the server", "is the box ok".
short: 'Probe a remote host over SSH: services, disk, memory, certificates; report what is out of range.'
profile: |
  artifact: system
  primary: probe
  also: script
  human: none
  sign_off: none
  effort: low
  stakes: nuisance
  blast: case
  regularity: routine
  executable: yes
  rot: medium
  rot_reason: hosts, services and thresholds get renamed
  confidential: no
  money: no
  location: remote
---

# Server health check

Every command is read-only; this skill changes nothing on the host.

1. Confirm the SSH host from the request and that it answers.
2. Uptime, load average and memory in use.
3. Disk use per mounted filesystem; flag anything above 85 percent.
4. The services the host is expected to run, from its own list, and whether each is active.
5. TLS certificate expiry for every listening HTTPS service; flag under 14 days.
6. Pending security updates, if the package manager reports them.
7. Report one line per check with its value, and the out-of-range ones first. A fault is a card
   for the person, not a repair.
