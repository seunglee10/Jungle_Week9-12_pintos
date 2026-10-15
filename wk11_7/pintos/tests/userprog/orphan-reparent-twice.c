/* Reparenting debug case: wait twice on a reparented orphan. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) {
	pid_t parent;
	int orphan_pid;

	parent = fork ("orphan-parent");
	if (parent == 0)
		exec ("orphan-parent child-simple");

	CHECK (parent > 0, "fork orphan-parent");
	orphan_pid = wait (parent);
	msg ("wait(parent) -> orphan pid = %d", orphan_pid);
	msg ("first wait(orphan) = %d", wait (orphan_pid));
	msg ("second wait(orphan) = %d", wait (orphan_pid));
}
