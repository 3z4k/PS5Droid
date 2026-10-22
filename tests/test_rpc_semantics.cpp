#include "android/binder/binder_engine.h"
#include "test_framework.h"
#include <atomic>
#include <stdexcept>
#include <thread>
using namespace ps5droid::android;
namespace {
std::vector<uint8_t> echo(uint32_t, const std::vector<uint8_t>& in, void*) {
    return in;
}
std::vector<uint8_t> throwing(uint32_t, const std::vector<uint8_t>&, void*) {
    throw std::runtime_error("handler");
}
struct Nested {
    BinderEngine* engine;
    uint32_t handle;
    RpcStatus status;
};
std::vector<uint8_t> nested(uint32_t, const std::vector<uint8_t>& in, void* p) {
    auto& n = *static_cast<Nested*>(p);
    auto r = n.engine->transact_checked(n.handle, 1, in);
    n.status = r.status;
    return r.reply;
}
} // namespace
PS5DROID_TEST(rpc_missing_and_reserved_handles_never_succeed) {
    BinderEngine engine;
    CHECK(engine.lookup_service("missing") == BinderEngine::kInvalidHandle);
    CHECK(engine.lookup_service("servicemanager") == BinderEngine::kInvalidHandle);
    std::vector<uint8_t> output{1};
    CHECK(!engine.transact(0, 1, {}, &output));
    CHECK(output.empty());
    CHECK(engine.register_service("servicemanager", echo) == BinderEngine::kInvalidHandle);
    CHECK(engine.register_service("", echo) == BinderEngine::kInvalidHandle);
    CHECK(engine.register_service("null", nullptr) == BinderEngine::kInvalidHandle);
}
PS5DROID_TEST(rpc_rejects_payload_overflow_flags_and_allocation_failure) {
    BinderEngine engine(1, 0);
    uint32_t h = engine.register_service("echo", echo);
    CHECK(engine.transact_checked(h, 1, {}).status == RpcStatus::kNoMemory);
    CHECK(engine.transact_checked(h, 1, {}, 1).status == RpcStatus::kUnsupportedFlags);
    CHECK(engine.transact_checked(h, 1, std::vector<uint8_t>(257)).status == RpcStatus::kTooLarge);
}
PS5DROID_TEST(rpc_releases_buffers_after_handler_exception) {
    BinderEngine engine(1, 256);
    auto h = engine.register_service("throws", throwing);
    CHECK(engine.transact_checked(h, 1, {}).status == RpcStatus::kHandlerError);
    auto e = engine.register_service("echo", echo);
    CHECK(engine.transact_checked(e, 1, {1, 2}).reply == std::vector<uint8_t>({1, 2}));
}
PS5DROID_TEST(rpc_nested_calls_have_explicit_capacity_failure) {
    BinderEngine engine(1, 256);
    auto e = engine.register_service("echo", echo);
    Nested n{&engine, e, RpcStatus::kOk};
    auto h = engine.register_service("nested", nested, &n);
    CHECK(engine.transact_checked(h, 1, {1}).ok());
    CHECK(n.status == RpcStatus::kNoMemory);
}
PS5DROID_TEST(rpc_nested_calls_work_without_engine_lock_deadlock) {
    BinderEngine engine(2, 512);
    auto e = engine.register_service("echo", echo);
    Nested n{&engine, e, RpcStatus::kNoMemory};
    auto h = engine.register_service("nested", nested, &n);
    auto result = engine.transact_checked(h, 1, {7});
    CHECK(result.ok() && result.reply == std::vector<uint8_t>({7}));
    CHECK(n.status == RpcStatus::kOk);
}
PS5DROID_TEST(rpc_concurrent_calls_do_not_corrupt_buffers) {
    BinderEngine engine(8, 8 * 256);
    auto h = engine.register_service("echo", echo);
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
        threads.emplace_back([&, i] {
            std::vector<uint8_t> input(256, static_cast<uint8_t>(i));
            for (int j = 0; j < 100; ++j) {
                auto r = engine.transact_checked(h, 1, input);
                if (!r.ok() || r.reply != input)
                    ++errors;
            }
        });
    for (auto& thread : threads)
        thread.join();
    CHECK(errors == 0);
}
