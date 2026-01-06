#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <unordered_map>

#include "types/common.hpp"
#include "types/fixed_string.hpp"

class FixedStringTest : public ::testing::Test {
 protected:
  using TestString = FixedString<16>;
};

TEST_F(FixedStringTest, DefaultConstruction) {
  TestString fs;
  EXPECT_TRUE(fs.empty());
  EXPECT_EQ(std::string_view(fs), "");
}

TEST_F(FixedStringTest, ConstructionFromStringView) {
  TestString fs("hello");
  EXPECT_FALSE(fs.empty());
  EXPECT_EQ(std::string_view(fs), "hello");
}

TEST_F(FixedStringTest, ConstructionFromConstCharPtr) {
  const char* str = "world";
  TestString fs(str);
  EXPECT_EQ(std::string_view(fs), "world");
}

TEST_F(FixedStringTest, ConstructionFromStdString) {
  std::string str = "test";
  TestString fs(str);
  EXPECT_EQ(std::string_view(fs), "test");
}

TEST_F(FixedStringTest, Assignment) {
  TestString fs;
  fs = std::string_view("assigned");
  EXPECT_EQ(std::string_view(fs), "assigned");
}

TEST_F(FixedStringTest, Truncation) {
  TestString fs("this string is way too long for the buffer");
  // Should truncate to 16 characters
  EXPECT_EQ(std::string_view(fs).size(), 16);
  EXPECT_EQ(std::string_view(fs), "this string is w");
}

TEST_F(FixedStringTest, EqualityComparison) {
  TestString a("abc");
  TestString b("abc");
  TestString c("xyz");

  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a == c);
  EXPECT_TRUE(a != c);
}

TEST_F(FixedStringTest, ComparisonWithStringView) {
  TestString fs("hello");
  EXPECT_TRUE(fs == std::string_view("hello"));
  EXPECT_FALSE(fs == std::string_view("world"));
}

TEST_F(FixedStringTest, LessThanComparison) {
  TestString a("abc");
  TestString b("abd");

  EXPECT_TRUE(a < b);
  EXPECT_FALSE(b < a);
}

TEST_F(FixedStringTest, CStr) {
  TestString fs("test");
  EXPECT_STREQ(fs.c_str(), "test");
}

TEST_F(FixedStringTest, ToStdString) {
  TestString fs("convert");
  std::string s = fs.str();
  EXPECT_EQ(s, "convert");
}

TEST_F(FixedStringTest, HashInUnorderedMap) {
  std::unordered_map<TestString, int> map;
  TestString key1("key1");
  TestString key2("key2");

  map[key1] = 100;
  map[key2] = 200;

  EXPECT_EQ(map[key1], 100);
  EXPECT_EQ(map[key2], 200);
  EXPECT_EQ(map.size(), 2);
}
