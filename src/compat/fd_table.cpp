// PS5Droid - Guest File Descriptor Table and Epoll implementation.
#include "compat/fd_table.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "platform/platform.h"

namespace ps5droid {
namespace compat {

namespace {

class FileFdEntry : public FdEntry {
  public:
    FileFdEntry(const std::string& path, FILE* fp) : path_(path), fp_(fp) {}
    ~FileFdEntry() override { close(); }

    FdType type() const override { return FdType::kFile; }

    int64_t read(void* buf, size_t count, int64_t offset) override {
        if (!fp_)
            return -1;
        if (offset >= 0) {
            fseek(fp_, static_cast<long>(offset), SEEK_SET);
        }
        size_t r = fread(buf, 1, count, fp_);
        return static_cast<int64_t>(r);
    }

    int64_t write(const void* buf, size_t count, int64_t offset) override {
        if (!fp_)
            return -1;
        if (offset >= 0) {
            fseek(fp_, static_cast<long>(offset), SEEK_SET);
        }
        size_t w = fwrite(buf, 1, count, fp_);
        fflush(fp_);
        return static_cast<int64_t>(w);
    }

    int64_t lseek(int64_t offset, int whence) override {
        if (!fp_)
            return -1;
        if (fseek(fp_, static_cast<long>(offset), whence) != 0) {
            return -1;
        }
        return static_cast<int64_t>(ftell(fp_));
    }

    bool fstat(StatBuf* statbuf) override {
        if (!statbuf || !fp_)
            return false;
        memset(statbuf, 0, sizeof(*statbuf));
        long cur = ftell(fp_);
        fseek(fp_, 0, SEEK_END);
        long sz = ftell(fp_);
        fseek(fp_, cur, SEEK_SET);
        statbuf->st_size = sz;
        statbuf->st_mode = 0100644; // S_IFREG
        return true;
    }

    void close() override {
        if (fp_) {
            fclose(fp_);
            fp_ = nullptr;
        }
    }

  private:
    std::string path_;
    FILE* fp_;
};

class StreamFdEntry : public FdEntry {
  public:
    explicit StreamFdEntry(FILE* stream) : stream_(stream) {}
    FdType type() const override { return FdType::kStream; }

    int64_t read(void*, size_t, int64_t) override { return -1; }
    int64_t write(const void* buf, size_t count, int64_t) override {
        if (!stream_ || !buf)
            return -1;
        size_t w = fwrite(buf, 1, count, stream_);
        fflush(stream_);
        return static_cast<int64_t>(w);
    }
    int64_t lseek(int64_t, int) override { return -1; }
    bool fstat(StatBuf* statbuf) override {
        if (!statbuf)
            return false;
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = 0020666; // S_IFCHR
        return true;
    }
    void close() override { stream_ = nullptr; }

  private:
    FILE* stream_;
};

class EpollFdEntry : public FdEntry {
  public:
    EpollFdEntry() = default;
    FdType type() const override { return FdType::kEpoll; }

    int64_t read(void*, size_t, int64_t) override { return -1; }
    int64_t write(const void*, size_t, int64_t) override { return -1; }
    int64_t lseek(int64_t, int) override { return -1; }
    bool fstat(StatBuf* statbuf) override {
        if (!statbuf)
            return false;
        memset(statbuf, 0, sizeof(*statbuf));
        return true;
    }
    void close() override { events_.clear(); }

    bool ctl(int op, int fd, const EpollEvent* event) {
        if (op == 1 /* EPOLL_CTL_ADD */) {
            if (!event)
                return false;
            events_[fd] = *event;
            return true;
        } else if (op == 2 /* EPOLL_CTL_DEL */) {
            events_.erase(fd);
            return true;
        } else if (op == 3 /* EPOLL_CTL_MOD */) {
            if (!event || events_.find(fd) == events_.end())
                return false;
            events_[fd] = *event;
            return true;
        }
        return false;
    }

    int wait(EpollEvent* out_events, int maxevents, int timeout_ms) {
        if (!out_events || maxevents <= 0)
            return -1;
        int count = 0;
        for (const auto& kv : events_) {
            if (count >= maxevents)
                break;
            out_events[count++] = kv.second;
        }
        if (count == 0 && timeout_ms > 0) {
            platform::sleep_ns(static_cast<uint64_t>(timeout_ms) * 1000000ull);
        }
        return count;
    }

  private:
    std::unordered_map<int, EpollEvent> events_;
};

class EventFdEntry : public FdEntry {
  public:
    explicit EventFdEntry(uint64_t initval) : counter_(initval) {}
    FdType type() const override { return FdType::kEventFd; }

    int64_t read(void* buf, size_t count, int64_t) override {
        if (count < sizeof(uint64_t) || !buf)
            return -1;
        uint64_t val = counter_;
        counter_ = 0;
        std::memcpy(buf, &val, sizeof(uint64_t));
        return sizeof(uint64_t);
    }

    int64_t write(const void* buf, size_t count, int64_t) override {
        if (count < sizeof(uint64_t) || !buf)
            return -1;
        uint64_t val = 0;
        std::memcpy(&val, buf, sizeof(uint64_t));
        counter_ += val;
        return sizeof(uint64_t);
    }

    int64_t lseek(int64_t, int) override { return -1; }
    bool fstat(StatBuf* statbuf) override {
        if (!statbuf)
            return false;
        memset(statbuf, 0, sizeof(*statbuf));
        return true;
    }
    void close() override { counter_ = 0; }

  private:
    uint64_t counter_ = 0;
};

} // namespace

CompatFdTable::CompatFdTable() {
    table_[0] = std::make_shared<StreamFdEntry>(stdin);
    table_[1] = std::make_shared<StreamFdEntry>(stdout);
    table_[2] = std::make_shared<StreamFdEntry>(stderr);
}

CompatFdTable::~CompatFdTable() {
    std::lock_guard<std::mutex> lock(mutex_);
    table_.clear();
}

int CompatFdTable::open_file(const std::string& path, int flags, int mode) {
    (void)mode;
    std::lock_guard<std::mutex> lock(mutex_);
    const char* fmode = "r";
    if ((flags & 0x3) == 1)
        fmode = "w";
    else if ((flags & 0x3) == 2)
        fmode = "r+";
    if (flags & 0x200 /* O_CREAT */) {
        if (flags & 0x400 /* O_TRUNC */)
            fmode = "w+";
        else
            fmode = "a+";
    }

    FILE* fp = fopen(path.c_str(), fmode);
    if (!fp)
        return -1;

    int fd = next_fd_++;
    table_[fd] = std::make_shared<FileFdEntry>(path, fp);
    return fd;
}

int CompatFdTable::create_epoll() {
    std::lock_guard<std::mutex> lock(mutex_);
    int fd = next_fd_++;
    table_[fd] = std::make_shared<EpollFdEntry>();
    return fd;
}

int CompatFdTable::create_eventfd(uint64_t initval, int flags) {
    (void)flags;
    std::lock_guard<std::mutex> lock(mutex_);
    int fd = next_fd_++;
    table_[fd] = std::make_shared<EventFdEntry>(initval);
    return fd;
}

bool CompatFdTable::epoll_ctl(int epfd, int op, int fd, const EpollEvent* event) {
    auto entry = get(epfd);
    if (!entry || entry->type() != FdType::kEpoll)
        return false;
    auto epoll = std::static_pointer_cast<EpollFdEntry>(entry);
    return epoll->ctl(op, fd, event);
}

int CompatFdTable::epoll_wait(int epfd, EpollEvent* events, int maxevents, int timeout_ms) {
    auto entry = get(epfd);
    if (!entry || entry->type() != FdType::kEpoll)
        return -1;
    auto epoll = std::static_pointer_cast<EpollFdEntry>(entry);
    return epoll->wait(events, maxevents, timeout_ms);
}

int64_t CompatFdTable::read(int fd, void* buf, size_t count) {
    auto entry = get(fd);
    if (!entry)
        return -1;
    return entry->read(buf, count, -1);
}

int64_t CompatFdTable::pread64(int fd, void* buf, size_t count, int64_t offset) {
    auto entry = get(fd);
    if (!entry)
        return -1;
    return entry->read(buf, count, offset);
}

int64_t CompatFdTable::write(int fd, const void* buf, size_t count) {
    auto entry = get(fd);
    if (!entry)
        return -1;
    return entry->write(buf, count, -1);
}

int64_t CompatFdTable::lseek(int fd, int64_t offset, int whence) {
    auto entry = get(fd);
    if (!entry)
        return -1;
    return entry->lseek(offset, whence);
}

bool CompatFdTable::fstat(int fd, StatBuf* statbuf) {
    auto entry = get(fd);
    if (!entry)
        return false;
    return entry->fstat(statbuf);
}

int CompatFdTable::dup(int oldfd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = table_.find(oldfd);
    if (it == table_.end())
        return -1;
    int newfd = next_fd_++;
    table_[newfd] = it->second;
    return newfd;
}

int CompatFdTable::dup2(int oldfd, int newfd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = table_.find(oldfd);
    if (it == table_.end() || newfd < 0)
        return -1;
    table_[newfd] = it->second;
    if (newfd >= next_fd_)
        next_fd_ = newfd + 1;
    return newfd;
}

int CompatFdTable::close(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = table_.find(fd);
    if (it == table_.end())
        return -1;
    it->second->close();
    table_.erase(it);
    return 0;
}

std::shared_ptr<FdEntry> CompatFdTable::get(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = table_.find(fd);
    if (it == table_.end())
        return nullptr;
    return it->second;
}

} // namespace compat
} // namespace ps5droid
