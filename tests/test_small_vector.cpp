#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "types/small_vector.hpp"

class SmallVectorTest : public ::testing::Test {
 protected:
  using TestVector = SmallVector<int, 4>;
};

TEST_F(SmallVectorTest, DefaultConstruction) {
  TestVector v;
  EXPECT_TRUE(v.empty());
  EXPECT_EQ(v.size(), 0);
  EXPECT_EQ(v.capacity(), 4);
}

TEST_F(SmallVectorTest, ConstructionWithCount) {
  SmallVector<int, 4> v(3);
  EXPECT_EQ(v.size(), 3);
  EXPECT_EQ(v[0], 0);
  EXPECT_EQ(v[1], 0);
  EXPECT_EQ(v[2], 0);
}

TEST_F(SmallVectorTest, ConstructionWithCountAndValue) {
  SmallVector<int, 4> v(3, 42);
  EXPECT_EQ(v.size(), 3);
  EXPECT_EQ(v[0], 42);
  EXPECT_EQ(v[1], 42);
  EXPECT_EQ(v[2], 42);
}

TEST_F(SmallVectorTest, InitializerListConstruction) {
  SmallVector<int, 4> v{1, 2, 3};
  EXPECT_EQ(v.size(), 3);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 2);
  EXPECT_EQ(v[2], 3);
}

TEST_F(SmallVectorTest, PushBackWithinCapacity) {
  TestVector v;
  v.push_back(1);
  v.push_back(2);
  v.push_back(3);
  v.push_back(4);

  EXPECT_EQ(v.size(), 4);
  EXPECT_EQ(v.capacity(), 4);  // Should still use stack
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[3], 4);
}

TEST_F(SmallVectorTest, PushBackExceedingCapacity) {
  TestVector v;
  for (int i = 0; i < 10; ++i) {
    v.push_back(i);
  }

  EXPECT_EQ(v.size(), 10);
  EXPECT_GE(v.capacity(), 10);  // Heap allocation
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(v[i], i);
  }
}

TEST_F(SmallVectorTest, PopBack) {
  TestVector v{1, 2, 3};
  v.pop_back();

  EXPECT_EQ(v.size(), 2);
  EXPECT_EQ(v.back(), 2);
}

TEST_F(SmallVectorTest, EmplaceBack) {
  SmallVector<std::string, 2> v;
  v.emplace_back("hello");
  v.emplace_back("world");

  EXPECT_EQ(v.size(), 2);
  EXPECT_EQ(v[0], "hello");
  EXPECT_EQ(v[1], "world");
}

TEST_F(SmallVectorTest, FrontAndBack) {
  TestVector v{10, 20, 30};
  EXPECT_EQ(v.front(), 10);
  EXPECT_EQ(v.back(), 30);
}

TEST_F(SmallVectorTest, AtBoundsCheck) {
  TestVector v{1, 2, 3};
  EXPECT_EQ(v.at(0), 1);
  EXPECT_EQ(v.at(2), 3);
  EXPECT_THROW(v.at(3), std::out_of_range);
}

TEST_F(SmallVectorTest, Clear) {
  TestVector v{1, 2, 3, 4};
  v.clear();

  EXPECT_TRUE(v.empty());
  EXPECT_EQ(v.size(), 0);
}

TEST_F(SmallVectorTest, Reserve) {
  TestVector v;
  v.reserve(100);

  EXPECT_GE(v.capacity(), 100);
  EXPECT_EQ(v.size(), 0);
}

TEST_F(SmallVectorTest, Resize) {
  TestVector v{1, 2};
  v.resize(5);

  EXPECT_EQ(v.size(), 5);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 2);
  EXPECT_EQ(v[2], 0);
}

TEST_F(SmallVectorTest, ResizeWithValue) {
  TestVector v{1};
  v.resize(4, 99);

  EXPECT_EQ(v.size(), 4);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 99);
  EXPECT_EQ(v[3], 99);
}

TEST_F(SmallVectorTest, CopyConstruction) {
  TestVector original{1, 2, 3};
  TestVector copy(original);

  EXPECT_EQ(copy.size(), 3);
  EXPECT_EQ(copy[0], 1);
  EXPECT_EQ(copy[2], 3);
}

TEST_F(SmallVectorTest, MoveConstruction) {
  TestVector original{1, 2, 3};
  TestVector moved(std::move(original));

  EXPECT_EQ(moved.size(), 3);
  EXPECT_EQ(moved[0], 1);
  EXPECT_EQ(original.size(), 0);
}

TEST_F(SmallVectorTest, CopyAssignment) {
  TestVector a{1, 2};
  TestVector b{5, 6, 7, 8};
  a = b;

  EXPECT_EQ(a.size(), 4);
  EXPECT_EQ(a[3], 8);
}

TEST_F(SmallVectorTest, MoveAssignment) {
  TestVector a{1, 2};
  TestVector b{5, 6, 7};
  a = std::move(b);

  EXPECT_EQ(a.size(), 3);
  EXPECT_EQ(a[0], 5);
  EXPECT_EQ(b.size(), 0);
}

TEST_F(SmallVectorTest, IteratorAccess) {
  TestVector v{10, 20, 30};
  std::vector<int> collected(v.begin(), v.end());

  EXPECT_EQ(collected.size(), 3);
  EXPECT_EQ(collected[0], 10);
  EXPECT_EQ(collected[2], 30);
}

TEST_F(SmallVectorTest, RangeBasedFor) {
  TestVector v{1, 2, 3};
  int sum = 0;
  for (int x : v) {
    sum += x;
  }
  EXPECT_EQ(sum, 6);
}

TEST_F(SmallVectorTest, ShrinkToFit) {
  TestVector v;
  // Exceed capacity to trigger heap allocation
  for (int i = 0; i < 10; ++i) {
    v.push_back(i);
  }
  EXPECT_GE(v.capacity(), 10);

  // Shrink back to inline capacity
  v.clear();
  v.push_back(1);
  v.push_back(2);
  v.shrink_to_fit();

  EXPECT_EQ(v.size(), 2);
  EXPECT_EQ(v.capacity(), 4);  // Back to inline
}

TEST_F(SmallVectorTest, InlineCapacity) {
  EXPECT_EQ(TestVector::inline_capacity(), 4);
  EXPECT_EQ((SmallVector<int, 10>::inline_capacity()), 10);
}
