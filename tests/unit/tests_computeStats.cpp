// tests_computeStats.cpp -- Catch2 unit tests for computeStats function

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "diffdbg.hpp"

using namespace diffdbg;

// ---- tests: computeStats (Catch2) --------------------------------------------

TEST_CASE("computeStats empty vector", "[computeStats]") {
  std::vector<double> empty;
  StatsPayload s = computeStats(empty.data(), empty.size());
  
  REQUIRE(s.min == 0.0);
  REQUIRE(s.max == 0.0);
  REQUIRE(s.mean == 0.0);
  REQUIRE(s.nonFinite == 0ULL);
}

TEST_CASE("computeStats two elements at double max", "[computeStats]") {
  const double maxVal = std::numeric_limits<double>::max();
  std::vector<double> v{maxVal, maxVal};
  StatsPayload s = computeStats(v.data(), v.size());
  
  REQUIRE(s.min == maxVal);
  REQUIRE(s.max == maxVal);
  REQUIRE(s.mean == maxVal);
  REQUIRE(s.nonFinite == 0ULL);
}

TEST_CASE("computeStats with one NaN value", "[computeStats]") {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  std::vector<double> v{1.0, 2.0, nan, 4.0};
  StatsPayload s = computeStats(v.data(), v.size());
  
  REQUIRE(s.min == 1.0);
  REQUIRE(s.max == 4.0);
  REQUIRE(s.mean == Catch::Approx(7.0 / 3.0));  // (1+2+4)/3
  REQUIRE(s.nonFinite == 1ULL);
}

TEST_CASE("computeStats with all NaN values", "[computeStats]") {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  std::vector<double> v{nan, nan, nan};
  StatsPayload s = computeStats(v.data(), v.size());
  
  REQUIRE(s.min == 0.0);
  REQUIRE(s.max == 0.0);
  REQUIRE(s.mean == 0.0);
  REQUIRE(s.nonFinite == 3ULL);
}

TEST_CASE("computeStats with NaN and Inf", "[computeStats]") {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  std::vector<double> v{1.0, 2.0, 3.0, nan, inf};
  StatsPayload s = computeStats(v.data(), v.size());
  
  REQUIRE(s.min == 1.0);
  REQUIRE(s.max == 3.0);
  REQUIRE(s.mean == 2.0);
  REQUIRE(s.nonFinite == 2ULL);
}

TEST_CASE("computeStats with negative values", "[computeStats]") {
  std::vector<int> v{-5, 5, 10};
  StatsPayload s = computeStats(v.data(), v.size());
  
  REQUIRE(s.min == -5.0);
  REQUIRE(s.max == 10.0);
  REQUIRE(s.mean == Catch::Approx(10.0 / 3.0));
  REQUIRE(s.nonFinite == 0ULL);
}

TEST_CASE("computeStats single element", "[computeStats]") {
  std::vector<double> v{42.0};
  StatsPayload s = computeStats(v.data(), v.size());
  
  REQUIRE(s.min == 42.0);
  REQUIRE(s.max == 42.0);
  REQUIRE(s.mean == 42.0);
  REQUIRE(s.nonFinite == 0ULL);
}

TEST_CASE("computeStats floating point precision", "[computeStats]") {
  std::vector<double> v{0.1, 0.2, 0.3};
  StatsPayload s = computeStats(v.data(), v.size());
  
  REQUIRE(s.min == Catch::Approx(0.1));
  REQUIRE(s.max == Catch::Approx(0.3));
  REQUIRE(s.mean == Catch::Approx(0.2));
  REQUIRE(s.nonFinite == 0ULL);
}

TEST_CASE("computeStats with symmetric large values", "[computeStats]") {
  const double largeVal = 1e300;
  std::vector<double> v{-largeVal, largeVal};
  StatsPayload s = computeStats(v.data(), v.size());
  
  REQUIRE(s.min == -largeVal);
  REQUIRE(s.max == largeVal);
  REQUIRE(s.mean == Catch::Approx(0.0));  // symmetric around zero
  REQUIRE(s.nonFinite == 0ULL);
}
