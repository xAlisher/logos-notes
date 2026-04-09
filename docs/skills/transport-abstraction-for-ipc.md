# Skill: Transport Abstraction for Cross-Module IPC

**When to use:** Any time notes plugin calls another Logos module via
`LogosAPIClient::invokeRemoteMethod()` and you want unit tests that
don't link the Logos SDK.

## The pattern

1. Define a minimal virtual interface that only exposes the methods
   and signals the consumer actually needs. Call it `FooTransport`.
2. Split implementations into two TUs:
   - `LogosFooTransport.cpp` — real impl, forwards to `LogosAPIClient`.
     Only linked into `notes_plugin.so`.
   - `MockFooTransport` in the test file — records calls, fires
     synthetic events on demand.
3. Consumer class (`FooClient`) takes a `std::unique_ptr<FooTransport>`
   in its constructor — injected, owned.
4. In CMakeLists, add the real `LogosFooTransport.cpp` only to
   `PLUGIN_SOURCES`. Do NOT add it to any test target. Tests compile
   `FooClient.cpp` directly and provide their own mock.

## Why this is worth the one extra TU

- Test executable compiles in seconds, runs in milliseconds, no
  network / no running modules / no SDK headers required for the
  test translation unit.
- The consumer class exercises its real code path end-to-end (via
  the mock's synthetic events). It is not a subclass-for-test dodge.
- Mock can simulate states the real module rarely produces: dropped
  events, stray events, busy, timeouts, corrupt payloads. Harder to
  hit with live integration tests.
- Concurrency / timeout contracts can be unit-tested deterministically.

## Reference implementation

- `src/core/StorageClient.{h,cpp}` — consumer
- `src/core/LogosStorageTransport.cpp` — real transport (separate TU)
- `tests/test_storage_client.cpp::MockStorageTransport` — test transport

## CMake wiring (copy-paste form)

```cmake
# In PLUGIN_SOURCES
src/core/FooClient.h
src/core/FooClient.cpp
src/core/LogosFooTransport.cpp   # real impl — plugin only

# Test target — does NOT list LogosFooTransport.cpp
add_executable(test_foo_client
    tests/test_foo_client.cpp
    src/core/FooClient.h
    src/core/FooClient.cpp
)
target_link_libraries(test_foo_client PRIVATE
    Qt6::Core
    Qt6::Test
)
```

## Concurrency contract that falls out of this pattern

Because tests can fire synthetic events at will, the consumer should
enforce a clear single-in-flight or request-correlated contract and
the tests should exercise every edge:

- Concurrent call while pending → synchronous rejection ("busy")
- Timeout (no terminal event) → callback fires with error
- Stray terminal event (no pending request) → ignored silently
- Destructor with pending request → callback fires with error

Each of these is a 5-10 line test, all together catch the async holes
Senty flagged on `#71` round 3.
