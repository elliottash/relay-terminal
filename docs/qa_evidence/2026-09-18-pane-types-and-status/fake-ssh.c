/* SPDX-License-Identifier: AGPL-3.0-or-later
   A stand-in for ssh in the screenshots (there is no sshd to log in to under Xvfb). Like ssh it
   stays in the terminal's foreground process group and relays a session on a pty of its own
   (script(1) here), so Relay reads exactly what it would read for a real one: the foreground
   program is `ssh` and its command line is "ssh demo@build-box". The shell inside is local. */
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    char rc[512], command[1200];
    snprintf(rc, sizeof rc, "%s/fake-ssh.rc", tmp);
    FILE *file = fopen(rc, "w");
    if (file) { fputs("PS1='demo@build-box:~$ '\n", file); fclose(file); }
    snprintf(command, sizeof command, "exec script -qfec 'bash --noprofile --rcfile %s -i' /dev/null", rc);
    return system(command);
}
