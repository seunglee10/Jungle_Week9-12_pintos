/* Reparenting stress case: repeat orphan handoff many times. */

#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) {
	int i;

	for (i = 0; i < 10; i++) {
		pid_t parent = fork ("orphan-parent");
		int orphan_pid;
		int status;

		if (parent == 0)
			exec ("orphan-parent child-simple");

		CHECK (parent > 0, "fork orphan-parent #%d", i);
		orphan_pid = wait (parent);
		msg ("[%d] wait(parent) -> orphan pid = %d", i, orphan_pid);
		status = wait (orphan_pid);
		msg ("[%d] wait(orphan) = %d", i, status);
	}
}
