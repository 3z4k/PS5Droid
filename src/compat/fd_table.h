// PS5Droid - Guest File Descriptor Table and Epoll abstraction.
//
// Milestone 3: In-process file descriptor table managing simulated guest fds
// (files, streams, pipes, eventfds, epoll instances) and multiplexing epoll
// event loops over kqueue / host poll primitives.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ps5droid {
namespace compat {

enum class FdType {
    kFile,
    kStream,
    kEpoll,
    kEventFd,
};

struct StatBuf {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime_sec;
    int64_t st_mtime_sec;
    int64_t st_ctime_sec;
};

struct EpollEvent {
    uint32_t events;
    uint64_t data;
};

class FdEntry {
  public:
    virtual ~FdEntry() = default;
    virtual FdType type() const = 0;
    virtual int64_t read(void* buf, size_t count, int64_t offset = -1) = 0;
    virtual int64_t write(const void* buf, size_t count, int64_t offset = -1) = 0;
    virtual int64_t lseek(int64_t offset, int whence) = 0;
    virtual bool fstat(StatBuf* statbuf) = 0;
    virtual void close() = 0;
};

class CompatFdTable {
  public:
    CompatFdTable();
    ~CompatFdTable();

    int open_file(const std::string& path, int flags, int mode);
    int create_epoll();
    int create_eventfd(uint64_t initval, int flags);

    bool epoll_ctl(int epfd, int op, int fd, const EpollEvent* event);
    int epoll_wait(int epfd, EpollEvent* events, int maxevents, int timeout_ms);

    int64_t read(int fd, void* buf, size_t count);
    int64_t pread64(int fd, void* buf, size_t count, int64_t offset);
    int64_t write(int fd, const void* buf, size_t count);
    int64_t lseek(int fd, int64_t offset, int whence);
    bool fstat(int fd, StatBuf* statbuf);

    int dup(int oldfd);
    int dup2(int oldfd, int newfd);
    int close(int fd);

    std::shared_ptr<FdEntry> get(int fd);

  private:
    std::mutex mutex_;
    int next_fd_ = 3; // 0=stdin, 1=stdout, 2=stderr
    std::unordered_map<int, std::shared_ptr<FdEntry>> table_;
};

} // namespace compat
} // namespace ps5droid
