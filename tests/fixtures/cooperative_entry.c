/* Freestanding Android-target ELF fixture. No bionic or Android framework.
 * The callback replaces syscall stubs explicitly; raw syscalls are not patched. */
typedef unsigned long long u64;
typedef long long i64;
typedef i64 (*gateway_fn)(void*, int, const u64*);
__attribute__((visibility("hidden"))) i64
ps5droid_fixture_entry(void* context, gateway_fn gateway, const unsigned char* initial_stack) {
    u64 args[6] = {0};
    if (*(const u64*)initial_stack != 1)
        return 10;
    const char* name = (const char*)((const u64*)initial_stack)[1];
    if (name[0] != 'p')
        return 11;
    i64 pid = gateway(context, 39, args);
    if (pid <= 0)
        return 12;
    static const char message[] = "PS5Droid cooperative ELF: entry -> write -> exit_group\n";
    args[0] = 1;
    args[1] = (u64)message;
    args[2] = sizeof(message) - 1;
    if (gateway(context, 1, args) != (i64)(sizeof(message) - 1))
        return 13;
    args[0] = 0;
    return gateway(context, 231, args);
}
