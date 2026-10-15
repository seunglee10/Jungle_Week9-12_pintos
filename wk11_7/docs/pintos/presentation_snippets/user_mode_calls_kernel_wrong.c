/*
 * Presentation snippet:
 * "Mistake: trying to call kernel-internal functions directly from user mode"
 *
 * This file is intentionally illustrative, not meant to compile.
 */

/* Wrong idea: user code directly touches kernel-internal process helpers. */
int main(void) {
    /*
     * process_fork(), process_wait(), process_exit() are kernel-side helpers.
     * A user program must not call them directly.
     */
    process_fork("child", NULL);
    process_wait(1);
    process_exit();
    return 0;
}

/*
 * Correct direction:
 * user mode -> system call -> syscall handler -> kernel helper
 */
int user_main(void) {
    pid_t pid = fork("child");
    wait(pid);
    exit(0);
}

/*
 * What must happen in Pintos:
 *
 *   user mode call
 *       fork("child")
 *           ->
 *   syscall entry
 *       SYS_FORK
 *           ->
 *   kernel handler
 *       syscall_handler()
 *           ->
 *   kernel helper
 *       process_fork()
 */
