/*
 * Setuid helper: verify a user's password against /etc/shadow.
 * Usage: br8-chkpass <username> <password>
 * Exit 0 on match, 1 otherwise.
 */
#define _GNU_SOURCE
#include <crypt.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 3 || !argv[1][0] || !argv[2][0]) {
        fprintf(stderr, "usage: br8-chkpass <username> <password>\n");
        return 1;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "br8-chkpass: must run setuid root\n");
        return 1;
    }

    struct spwd *sp = getspnam(argv[1]);
    if (!sp || !sp->sp_pwdp || !sp->sp_pwdp[0])
        return 1;
    if (strcmp(sp->sp_pwdp, "!") == 0 || strcmp(sp->sp_pwdp, "*") == 0)
        return 1;

    char *enc = crypt(argv[2], sp->sp_pwdp);
    if (!enc)
        return 1;
    return strcmp(enc, sp->sp_pwdp) == 0 ? 0 : 1;
}
