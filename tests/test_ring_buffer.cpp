#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "types/ring_buffer.hpp"

class RingBufferTest : public ::testing::Test {
 protected:
  RingBuffer<int, 8> buffer;  // Capacity is 7 (N-1 for SPSC)
};

TEST_F(RingBufferTest, EmptyOnConstruction) {
  EXPECT_TRUE(buffer.empty());
  EXPECT_EQ(buffer.size(), 0);
}

TEST_F(RingBufferTest, PushAndPop) {
  EXPECT_TRUE(buffer.push(42));
  EXPECT_FALSE(buffer.empty());
  EXPECT_EQ(buffer.size(), 1);

  auto value = buffer.pop();
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, 42);
  EXPECT_TRUE(buffer.empty());
}

TEST_F(RingBufferTest, PushUntilFull) {
  // Capacity is N-1 = 7
  for (int i = 0; i < 7; ++i) {
    EXPECT_TRUE(buffer.push(i)) << "Failed at index " << i;
  }

  // Buffer should now be full
  EXPECT_FALSE(buffer.push(100));
  EXPECT_EQ(buffer.size(), 7);
}

TEST_F(RingBufferTest, PopFromEmpty) {
  auto value = buffer.pop();
  EXPECT_FALSE(value.has_value());
}

TEST_F(RingBufferTest, FIFO_Order) {
  buffer.push(1);
  buffer.push(2);
  buffer.push(3);

  EXPECT_EQ(*buffer.pop(), 1);
  EXPECT_EQ(*buffer.pop(), 2);
  EXPECT_EQ(*buffer.pop(), 3);
}

TEST_F(RingBufferTest, Wraparound) {
  // Fill and drain multiple times to test wraparound
  for (int round = 0; round < 5; ++round) {
    for (int i = 0; i < 7; ++i) {
      EXPECT_TRUE(buffer.push(round * 10 + i));
    }
    for (int i = 0; i < 7; ++i) {
      auto value = buffer.pop();
      EXPECT_TRUE(value.has_value());
      EXPECT_EQ(*value, round * 10 + i);
    }
    EXPECT_TRUE(buffer.empty());
  }
}

TEST_F(RingBufferTest, MoveSemantics) {
  struct MoveOnly {
    int value;
    MoveOnly() : value(-1) {}  // Default constructor required for ring buffer storage
    MoveOnly(int v) : value(v) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& other) noexcept : value(other.value) { other.value = -1; }
    MoveOnly& operator=(MoveOnly&& other) noexcept {
      value = other.value;
      other.value = -1;
      return *this;
    }
  };

  RingBuffer<MoveOnly, 4> move_buffer;
  move_buffer.push(MoveOnly{42});

  auto popped = move_buffer.pop();
  EXPECT_TRUE(popped.has_value());
  EXPECT_EQ(popped->value, 42);
}

TEST_F(RingBufferTest, Capacity) {
  // Wrap template expressions in parentheses to avoid macro parsing issues
  EXPECT_EQ((RingBuffer<int, 8>::capacity()), 7);
  EXPECT_EQ((RingBuffer<int, 16>::capacity()), 15);
  EXPECT_EQ((RingBuffer<int, 1024>::capacity()), 1023);
}

TEST(RingBufferDynamicTest, RuntimeCapacityAndFIFO) {
  RingBuffer<int, 0> buffer(7);

  EXPECT_EQ(buffer.capacity(), 7);
  for (int i = 0; i < 7; ++i) {
    EXPECT_TRUE(buffer.push(i));
  }
  EXPECT_FALSE(buffer.push(7));

  for (int i = 0; i < 7; ++i) {
    auto value = buffer.pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, i);
  }
  EXPECT_TRUE(buffer.empty());
}

TEST(RingBufferDynamicTest, RuntimeBufferDoesNotResizeWhenFull) {
  RingBuffer<int, 0> buffer(3);

  EXPECT_EQ(buffer.capacity(), 3);
  EXPECT_TRUE(buffer.push(1));
  EXPECT_TRUE(buffer.push(2));
  EXPECT_TRUE(buffer.push(3));

  EXPECT_FALSE(buffer.push(4));
  EXPECT_EQ(buffer.capacity(), 3);
  EXPECT_EQ(buffer.size(), 3);
}

// Multi-threaded test for SPSC correctness
TEST(RingBufferConcurrencyTest, ProducerConsumer) {
  RingBuffer<int, 1024> buffer;
  constexpr int kNumItems = 10000;
  std::atomic<bool> producer_done{false};
  std::vector<int> consumed;
  consumed.reserve(kNumItems);

  // Producer thread
  std::thread producer([&]() {
    for (int i = 0; i < kNumItems; ++i) {
      buffer.push_wait(std::move(i));
    }
    producer_done.store(true, std::memory_order_release);
  });

  // Consumer thread
  std::thread consumer([&]() {
    while (!producer_done.load(std::memory_order_acquire) || !buffer.empty()) {
      if (auto value = buffer.pop()) {
        consumed.push_back(*value);
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();

  // Verify all items received in order
  ASSERT_EQ(consumed.size(), kNumItems);
  for (int i = 0; i < kNumItems; ++i) {
    EXPECT_EQ(consumed[i], i) << "Mismatch at index " << i;
  }
}

TEST(RingBufferBlockingTest, PushWaitCallsCallback) {
  RingBuffer<int, 4> buffer;  // Capacity 3

  // Fill the buffer
  buffer.push(1);
  buffer.push(2);
  buffer.push(3);

  bool callback_called = false;

  // Start a thread that will push_wait
  std::thread pusher([&]() {
    buffer.push_wait(4, [&]() { callback_called = true; });
  });

  // Give the pusher time to block
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Pop one element to unblock
  buffer.pop();

  pusher.join();

  EXPECT_TRUE(callback_called);
}
