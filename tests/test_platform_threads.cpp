// PS5Droid - platform thread/synchronization tests.
#include "test_framework.h"

#include <atomic>
#include <thread>

#include "platform/platform.h"

using namespace ps5droid::platform;

namespace {
struct Ctx {
    std::atomic<int>* counter;
    int iters;
};
void counter_fn(void* raw) {
    Ctx* c = static_cast<Ctx*>(raw);
    for (int i = 0; i < c->iters; ++i) {
        c->counter->fetch_add(1, std::memory_order_relaxed);
    }
}
} // namespace

PS5DROID_TEST(thread_start_join) {
    std::atomic<int> counter{0};
    Ctx ctx{&counter, 5000};
    Thread t;
    CHECK_MSG(t.start(counter_fn, &ctx), "thread start failed");
    CHECK_MSG(t.join(), "thread join failed");
    CHECK_MSG(counter.load() == 5000, "counter mismatch");
    CHECK_MSG(!t.join(), "double join must fail");
}

PS5DROID_TEST(four_threads) {
    std::atomic<int> counter{0};
    Ctx ctx{&counter, 2000};
    Thread threads[4];
    for (auto& t : threads)
        CHECK_MSG(t.start(counter_fn, &ctx), "start");
    for (auto& t : threads)
        CHECK_MSG(t.join(), "join");
    CHECK_MSG(counter.load() == 8000, "counter mismatch");
}

PS5DROID_TEST(mutex_cond_producer_consumer) {
    Mutex mutex;
    CondVar cv;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> done{false};

    Thread consumer;
    struct C {
        Mutex* m;
        CondVar* cv;
        std::atomic<int>* produced;
        std::atomic<int>* consumed;
        std::atomic<bool>* done;
    } c{&mutex, &cv, &produced, &consumed, &done};

    auto consumer_fn = [](void* raw) {
        C* c = static_cast<C*>(raw);
        while (true) {
            c->m->lock();
            while (c->produced->load() == c->consumed->load() && !c->done->load()) {
                c->cv->wait(*c->m);
            }
            bool more = c->consumed->load() < c->produced->load();
            if (more)
                c->consumed->fetch_add(1);
            c->m->unlock();
            if (!more && c->done->load())
                break;
        }
    };
    CHECK_MSG(consumer.start(consumer_fn, &c), "consumer start failed");

    for (int i = 0; i < 1000; ++i) {
        produced.fetch_add(1);
        mutex.lock();
        cv.notify_one();
        mutex.unlock();
    }
    // drain window
    while (consumed.load() < 1000)
        ps5droid::platform::yield_thread();
    done.store(true);
    mutex.lock();
    cv.notify_all();
    mutex.unlock();
    CHECK_MSG(consumer.join(), "consumer join failed");
    CHECK_MSG(consumed.load() == 1000, "items lost");
}

PS5DROID_TEST(futex_wouldblock_and_wake) {
    Futex f;
    uint32_t w = 1;
    // value mismatch -> immediate EWOULDBLOCK
    CHECK_MSG(f.wait(&w, 2) == 1, "expected EWOULDBLOCK");
    // no waiters -> wake returns 0
    CHECK_MSG(f.wake(&w, 1) == 0, "expected zero waiters woken");
}

PS5DROID_TEST(futex_wake_waiter) {
    // Keep attempting wake until the waiter has enqueued; no shared-word race
    // or scheduler-dependent sleep is needed. CTest bounds a regression hang.
    static uint32_t word;
    word = 7;

    Thread waker;
    auto waker_fn = [](void*) {
        Futex f;
        while (f.wake(&word, 1) == 0)
            ps5droid::platform::yield_thread();
    };
    CHECK_MSG(waker.start(waker_fn, nullptr), "waker start failed");

    Futex f;
    int wr = f.wait(&word, 7); // sleeps until woken

    CHECK_MSG(waker.join(), "waker join failed");
    CHECK_MSG(wr == 0, "wait() must return 0 when woken");
    CHECK_MSG(word == 7, "wake does not modify the futex word");
}

PS5DROID_TEST(thread_ids_differ) {
    static std::atomic<uint32_t> other_id{0};
    Thread t;
    auto fn = [](void*) { other_id.store(current_thread_id()); };
    CHECK_MSG(t.start(fn, nullptr), "start failed");
    CHECK_MSG(t.join(), "join failed");
    CHECK_MSG(other_id.load() != 0, "no id recorded");
    CHECK_MSG(other_id.load() != current_thread_id(), "ids collide");
}
