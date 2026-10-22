// PS5Droid - Milestone 3 & 4 unit tests.
#include "elf_fixture.h"
#include "test_framework.h"

#include <cstring>

#include "compat/bionic_loader.h"
#include "compat/fd_table.h"
#include "platform/platform.h"

using namespace ps5droid::compat;

PS5DROID_TEST(fd_table_basic_rw_seek) {
    CompatFdTable table;
    std::string path = "test_fd_table.tmp";
    int fd = table.open_file(path, 0x40 | 0x200 | 0x2, 0644);
    CHECK_MSG(fd >= 3, "open_file failed");

    const char* msg = "hello fd_table";
    int64_t w = table.write(fd, msg, strlen(msg));
    CHECK_MSG(w == static_cast<int64_t>(strlen(msg)), "write failed");

    int64_t pos = table.lseek(fd, 0, 0);
    CHECK_MSG(pos == 0, "lseek failed");

    char buf[32] = {0};
    int64_t r = table.read(fd, buf, sizeof(buf));
    CHECK_MSG(r == static_cast<int64_t>(strlen(msg)), "read failed");
    CHECK_MSG(strcmp(buf, msg) == 0, "data mismatch");

    StatBuf st;
    CHECK_MSG(table.fstat(fd, &st), "fstat failed");
    CHECK_MSG(st.st_size == static_cast<int64_t>(strlen(msg)), "stat size mismatch");

    int dup_fd = table.dup(fd);
    CHECK_MSG(dup_fd > fd, "dup failed");

    table.close(fd);
    table.close(dup_fd);
    ps5droid::platform::remove_file(path);
}

PS5DROID_TEST(bionic_loader_hdr_and_stack) {
    auto elf_bytes = fixture::elf();
    BionicElfInfo info = BionicLoader::parse_header(elf_bytes.data(), elf_bytes.size());
    CHECK_MSG(info.valid, "ELF header parse failed");

    auto stack = BionicLoader::setup_initial_stack(info, {"app"}, {"ENV=1"});
    CHECK_MSG(!stack.empty(), "stack setup failed");
}
