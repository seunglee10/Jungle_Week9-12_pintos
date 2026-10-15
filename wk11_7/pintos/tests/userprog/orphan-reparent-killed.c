/* Reparenting debug case: orphaned child dies abnormally, root waits for -1. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) {
	pid_t parent;
	int orphan_pid;

	parent = fork ("orphan-parent");
	if (parent == 0)
		exec ("orphan-parent child-bad");

	CHECK (parent > 0, "fork orphan-parent");
	orphan_pid = wait (parent);
	msg ("wait(parent) -> orphan pid = %d", orphan_pid);
	msg ("wait(killed orphan) = %d", wait (orphan_pid));
}
