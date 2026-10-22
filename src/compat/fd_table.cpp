// Linux guest descriptors. Native flags and errno never cross the guest ABI.
#include "compat/fd_table.h"
#include "platform/platform.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

namespace ps5droid {
namespace compat {

struct FdSignal {
    std::mutex mutex;
    std::condition_variable changed;
    uint64_t generation = 0;
    void notify() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++generation;
        }
        changed.notify_all();
    }
};
namespace {
int bad(int error) {
    errno = error;
    return -1;
}

class NativeFd : public FdEntry {
  public:
    NativeFd(int fd, bool owned, FdType type) : fd_(fd), owned_(owned), type_(type) {}
    ~NativeFd() override {
        if (owned_)
            ::close(fd_);
    }
    FdType type() const override { return type_; }
    int64_t read(void* buf, size_t count, int64_t offset) override {
        if (count > static_cast<size_t>(std::numeric_limits<ssize_t>::max()))
            return bad(EINVAL);
        return offset < 0 ? ::read(fd_, buf, count) : ::pread(fd_, buf, count, offset);
    }
    int64_t write(const void* buf, size_t count, int64_t offset) override {
        if (count > static_cast<size_t>(std::numeric_limits<ssize_t>::max()))
            return bad(EINVAL);
        return offset < 0 ? ::write(fd_, buf, count) : ::pwrite(fd_, buf, count, offset);
    }
    int64_t lseek(int64_t offset, int whence) override { return ::lseek(fd_, offset, whence); }
    bool fstat(StatBuf* out) override {
        if (!out) {
            bad(EFAULT);
            return false;
        }
        struct stat st{};
        if (::fstat(fd_, &st) != 0)
            return false;
        *out = {};
        out->st_dev = st.st_dev;
        out->st_ino = st.st_ino;
        out->st_nlink = st.st_nlink;
        out->st_mode = st.st_mode;
        out->st_uid = st.st_uid;
        out->st_gid = st.st_gid;
        out->st_rdev = st.st_rdev;
        out->st_size = st.st_size;
        out->st_blksize = st.st_blksize;
        out->st_blocks = st.st_blocks;
        out->st_atime_sec = st.st_atime;
        out->st_mtime_sec = st.st_mtime;
        out->st_ctime_sec = st.st_ctime;
#if defined(__APPLE__)
        out->st_atime_nsec = st.st_atimespec.tv_nsec;
        out->st_mtime_nsec = st.st_mtimespec.tv_nsec;
        out->st_ctime_nsec = st.st_ctimespec.tv_nsec;
#else
        out->st_atime_nsec = st.st_atim.tv_nsec;
        out->st_mtime_nsec = st.st_mtim.tv_nsec;
        out->st_ctime_nsec = st.st_ctim.tv_nsec;
#endif
        return true;
    }
    // Lifetime belongs to the shared open-file description, not one descriptor.
    void close() override {}

  private:
    int fd_;
    bool owned_;
    FdType type_;
};

class EventFd : public FdEntry {
  public:
    EventFd(uint64_t value, int flags, std::shared_ptr<FdSignal> signal)
        : value_(value), flags_(flags), signal_(std::move(signal)) {}
    FdType type() const override { return FdType::kEventFd; }
    int64_t read(void* buf, size_t count, int64_t offset) override {
        if (offset >= 0)
            return bad(ESPIPE);
        if (count < 8)
            return bad(EINVAL);
        if (!buf)
            return bad(EFAULT);
        std::unique_lock<std::mutex> lock(mutex_);
        if (!value_ && (flags_ & 0x800))
            return bad(EAGAIN);
        changed_.wait(lock, [&] { return value_ != 0; });
        uint64_t value = (flags_ & 1) ? 1 : value_;
        value_ -= value;
        std::memcpy(buf, &value, 8);
        lock.unlock();
        changed_.notify_all();
        signal_->notify();
        return 8;
    }
    int64_t write(const void* buf, size_t count, int64_t offset) override {
        if (offset >= 0)
            return bad(ESPIPE);
        if (count != 8)
            return bad(EINVAL);
        if (!buf)
            return bad(EFAULT);
        uint64_t value;
        std::memcpy(&value, buf, 8);
        if (value == UINT64_MAX)
            return bad(EINVAL);
        std::unique_lock<std::mutex> lock(mutex_);
        auto fits = [&] { return value <= UINT64_MAX - 1 - value_; };
        if (!fits() && (flags_ & 0x800))
            return bad(EAGAIN);
        changed_.wait(lock, fits);
        value_ += value;
        lock.unlock();
        changed_.notify_all();
        signal_->notify();
        return 8;
    }
    uint32_t ready() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return (value_ ? 1u : 0u) | (value_ < UINT64_MAX - 1 ? 4u : 0u);
    }
    int64_t lseek(int64_t, int) override { return bad(ESPIPE); }
    bool fstat(StatBuf* out) override {
        if (!out) {
            bad(EFAULT);
            return false;
        }
        *out = {};
        out->st_mode = 0600;
        return true;
    }
    void close() override {}

  private:
    std::mutex mutex_;
    std::condition_variable changed_;
    uint64_t value_;
    int flags_;
    std::shared_ptr<FdSignal> signal_;
};

class EpollFd : public FdEntry {
  public:
    explicit EpollFd(std::shared_ptr<FdSignal> signal) : signal_(std::move(signal)) {}
    FdType type() const override { return FdType::kEpoll; }
    int64_t read(void*, size_t, int64_t) override { return bad(EINVAL); }
    int64_t write(const void*, size_t, int64_t) override { return bad(EINVAL); }
    int64_t lseek(int64_t, int) override { return bad(ESPIPE); }
    bool fstat(StatBuf* out) override {
        if (!out) {
            bad(EFAULT);
            return false;
        }
        *out = {};
        out->st_mode = 0600;
        return true;
    }
    void close() override {}
    bool ctl(int op, int fd, const std::shared_ptr<FdEntry>& target, const EpollEvent* event) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = events_.find(fd);
        if (it != events_.end() && it->second.target.expired()) {
            events_.erase(it);
            it = events_.end();
        }
        if (op < 1 || op > 3) {
            bad(EINVAL);
            return false;
        }
        if (op != 2 && (!event || (event->events & ~0x1du))) {
            bad(EINVAL);
            return false;
        }
        if (op == 1 && it != events_.end()) {
            bad(EEXIST);
            return false;
        }
        if (op != 1 && it == events_.end()) {
            bad(ENOENT);
            return false;
        }
        if (op == 2)
            events_.erase(it);
        else
            events_[fd] = Watch{target, *event};
        signal_->notify();
        return true;
    }
    int wait(EpollEvent* out, int maxevents, int timeout_ms) {
        if (!out)
            return bad(EFAULT);
        if (maxevents <= 0)
            return bad(EINVAL);
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(timeout_ms, 0));
        for (;;) {
            uint64_t generation;
            {
                std::lock_guard<std::mutex> lock(signal_->mutex);
                generation = signal_->generation;
            }
            int count = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto it = events_.begin(); it != events_.end();) {
                    auto target = it->second.target.lock();
                    if (!target) {
                        it = events_.erase(it);
                        continue;
                    }
                    uint32_t ready = target->ready() & (it->second.event.events | 0x18u);
                    if (ready && count < maxevents)
                        out[count++] = {ready, it->second.event.data};
                    ++it;
                }
            }
            if (count || timeout_ms == 0)
                return count;
            std::unique_lock<std::mutex> lock(signal_->mutex);
            auto changed = [&] { return signal_->generation != generation; };
            if (timeout_ms < 0)
                signal_->changed.wait(lock, changed);
            else if (!signal_->changed.wait_until(lock, deadline, changed))
                return 0;
        }
    }

  private:
    struct Watch {
        std::weak_ptr<FdEntry> target;
        EpollEvent event;
    };
    std::mutex mutex_;
    std::unordered_map<int, Watch> events_;
    std::shared_ptr<FdSignal> signal_;
};
} // namespace

CompatFdTable::CompatFdTable() : signal_(std::make_shared<FdSignal>()) {
    for (int fd = 0; fd < 3; ++fd)
        table_[fd] = std::make_shared<NativeFd>(fd, false, FdType::kStream);
}
CompatFdTable::~CompatFdTable() = default;
int CompatFdTable::insert(std::shared_ptr<FdEntry> entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    int fd = 0;
    while (table_.count(fd)) {
        if (fd == INT32_MAX)
            return bad(EMFILE);
        ++fd;
    }
    table_[fd] = std::move(entry);
    return fd;
}
int CompatFdTable::open_file(const std::string& path, int flags, int mode) {
    constexpr int allowed =
        3 | 0x40 | 0x80 | 0x100 | 0x200 | 0x400 | 0x800 | 0x8000 | 0x10000 | 0x20000 | 0x80000;
    if ((flags & ~allowed) || (flags & 3) == 3)
        return bad(EINVAL);
    int native = (flags & 3) == 2 ? O_RDWR : (flags & 3) == 1 ? O_WRONLY : O_RDONLY;
    const int guest[] = {0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x10000, 0x20000, 0x80000};
    const int host[] = {O_CREAT,    O_EXCL,      O_NOCTTY,   O_TRUNC,  O_APPEND,
                        O_NONBLOCK, O_DIRECTORY, O_NOFOLLOW, O_CLOEXEC};
    for (size_t i = 0; i < sizeof(guest) / sizeof(guest[0]); ++i)
        if (flags & guest[i])
            native |= host[i];
    int fd = ::open(path.c_str(), native, static_cast<mode_t>(mode & 07777));
    return fd < 0 ? -1 : insert(std::make_shared<NativeFd>(fd, true, FdType::kFile));
}
int CompatFdTable::create_epoll(int flags) {
    if (flags & ~0x80000)
        return bad(EINVAL);
    return insert(std::make_shared<EpollFd>(signal_));
}
int CompatFdTable::create_eventfd(uint64_t initval, int flags) {
    if ((flags & ~(1 | 0x800 | 0x80000)) || initval > UINT32_MAX)
        return bad(EINVAL);
    return insert(std::make_shared<EventFd>(initval, flags, signal_));
}
bool CompatFdTable::epoll_ctl(int epfd, int op, int fd, const EpollEvent* event) {
    auto ep = get(epfd);
    if (!ep)
        return false;
    auto target = get(fd);
    if (!target)
        return false;
    if (ep->type() != FdType::kEpoll || epfd == fd) {
        bad(EINVAL);
        return false;
    }
    // Only eventfd level-triggered readiness is implemented. Never fabricate file readiness.
    if (target->type() != FdType::kEventFd) {
        bad(EPERM);
        return false;
    }
    return std::static_pointer_cast<EpollFd>(ep)->ctl(op, fd, target, event);
}
int CompatFdTable::epoll_wait(int epfd, EpollEvent* events, int maxevents, int timeout_ms) {
    auto ep = get(epfd);
    if (!ep)
        return -1;
    if (ep->type() != FdType::kEpoll)
        return bad(EINVAL);
    return std::static_pointer_cast<EpollFd>(ep)->wait(events, maxevents, timeout_ms);
}
int64_t CompatFdTable::read(int fd, void* buf, size_t count) {
    auto e = get(fd);
    return e ? e->read(buf, count) : -1;
}
int64_t CompatFdTable::pread64(int fd, void* buf, size_t count, int64_t offset) {
    if (offset < 0)
        return bad(EINVAL);
    auto e = get(fd);
    return e ? e->read(buf, count, offset) : -1;
}
int64_t CompatFdTable::write(int fd, const void* buf, size_t count) {
    auto e = get(fd);
    return e ? e->write(buf, count) : -1;
}
int64_t CompatFdTable::lseek(int fd, int64_t offset, int whence) {
    if (whence < 0 || whence > 2)
        return bad(EINVAL);
    auto e = get(fd);
    return e ? e->lseek(offset, whence) : -1;
}
bool CompatFdTable::fstat(int fd, StatBuf* out) {
    auto e = get(fd);
    return e && e->fstat(out);
}
int CompatFdTable::dup(int oldfd) {
    auto e = get(oldfd);
    return e ? insert(e) : -1;
}
int CompatFdTable::dup2(int oldfd, int newfd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = table_.find(oldfd);
    if (it == table_.end() || newfd < 0)
        return bad(EBADF);
    auto entry = it->second;
    table_[newfd] = std::move(entry);
    signal_->notify();
    return newfd;
}
int CompatFdTable::close(int fd) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!table_.erase(fd))
            return bad(EBADF);
    }
    signal_->notify();
    return 0;
}
std::shared_ptr<FdEntry> CompatFdTable::get(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = table_.find(fd);
    if (it == table_.end()) {
        bad(EBADF);
        return nullptr;
    }
    return it->second;
}
} // namespace compat
} // namespace ps5droid
