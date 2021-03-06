/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/coro/Future.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/portability/GTest.h>

using namespace folly;

coro::Task<int> task42() {
  co_return 42;
}

TEST(Coro, Basic) {
  ManualExecutor executor;
  auto future = task42().via(&executor);

  EXPECT_FALSE(future.await_ready());

  executor.drive();

  EXPECT_TRUE(future.await_ready());
  EXPECT_EQ(42, future.get());
}

coro::Task<void> taskSleep() {
  co_await futures::sleep(std::chrono::seconds{1});
  co_return;
}

TEST(Coro, Sleep) {
  ScopedEventBaseThread evbThread;

  auto startTime = std::chrono::steady_clock::now();
  auto future = taskSleep().via(evbThread.getEventBase());

  EXPECT_FALSE(future.await_ready());

  future.wait();

  EXPECT_GE(
      std::chrono::steady_clock::now() - startTime, std::chrono::seconds{1});
  EXPECT_TRUE(future.await_ready());
}

coro::Task<void> taskException() {
  throw std::runtime_error("Test exception");
  co_return;
}

TEST(Coro, Throw) {
  ManualExecutor executor;
  auto future = taskException().via(&executor);

  EXPECT_FALSE(future.await_ready());

  executor.drive();

  EXPECT_TRUE(future.await_ready());
  EXPECT_THROW(future.get(), std::runtime_error);
}

coro::Task<int> taskRecursion(int depth) {
  if (depth > 0) {
    EXPECT_EQ(depth - 1, co_await taskRecursion(depth - 1));
  } else {
    co_await futures::sleep(std::chrono::seconds{1});
  }

  co_return depth;
}

TEST(Coro, LargeStack) {
  ScopedEventBaseThread evbThread;
  auto future = taskRecursion(10000).via(evbThread.getEventBase());

  future.wait();
  EXPECT_EQ(10000, future.get());
}

coro::Task<void> taskThreadNested(std::thread::id threadId) {
  EXPECT_EQ(threadId, std::this_thread::get_id());
  co_await futures::sleep(std::chrono::seconds{1});
  EXPECT_EQ(threadId, std::this_thread::get_id());
  co_return;
}

coro::Task<int> taskThread() {
  auto threadId = std::this_thread::get_id();

  folly::ScopedEventBaseThread evbThread;
  co_await taskThreadNested(evbThread.getThreadId())
      .via(evbThread.getEventBase());

  EXPECT_EQ(threadId, std::this_thread::get_id());

  co_return 42;
}

TEST(Coro, NestedThreads) {
  ScopedEventBaseThread evbThread;
  auto future = taskThread().via(evbThread.getEventBase());

  future.wait();
  EXPECT_EQ(42, future.get());
}

coro::Task<int> taskYield(Executor* executor) {
  auto currentExecutor = co_await coro::getCurrentExecutor();
  EXPECT_EQ(executor, currentExecutor);

  auto future = task42().via(currentExecutor);
  EXPECT_FALSE(future.await_ready());

  co_await coro::yield();

  EXPECT_TRUE(future.await_ready());
  co_return future.get();
}

TEST(Coro, CurrentExecutor) {
  ScopedEventBaseThread evbThread;
  auto future =
      taskYield(evbThread.getEventBase()).via(evbThread.getEventBase());

  future.wait();
  EXPECT_EQ(42, future.get());
}
