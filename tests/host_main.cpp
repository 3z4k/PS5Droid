// PS5Droid - unit test entry point (host).
#include "test_framework.h"

int main() {
    printf("PS5Droid unit tests\n");
    return testfw::run_all() ? 0 : 1;
}
