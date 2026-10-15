/* Helper program that forks one child, execs the requested program in the
   child, and exits with the child's pid so the original root process can
   try waiting on the reparented orphan. */

#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"

int
main (int argc, char *argv[]) {
	pid_t child_pid;

	test_name = "orphan-parent";

	if (argc < 2)
		fail ("missing child program name");

	child_pid = fork (argv[1]);
	if (child_pid < 0)
		fail ("fork(\"%s\") failed", argv[1]);

	if (child_pid == 0)
		exec (argv[1]);

	msg ("spawned orphan candidate pid=%d", child_pid);
	exit (child_pid);
}
