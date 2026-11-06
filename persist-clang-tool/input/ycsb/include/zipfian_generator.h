//
//  zipfian_generator.h
//  YCSB-C (Optimized version)
//
//  Notes:
//  - Removes shared RNG state
//  - Adds per-object mt19937_64
//  - Faster & thread-safe
//

#ifndef YCSB_C_ZIPFIAN_GENERATOR_H_
#define YCSB_C_ZIPFIAN_GENERATOR_H_

#include <cassert>
#include <cmath>
#include <cstdint>
#include <random>
#include <thread>
#include <chrono>
#include <functional>

namespace ycsbc {

template <typename Value>
class Generator {
 public:
  virtual Value Next() = 0;
  virtual Value Last() = 0;
  virtual ~Generator() { }
};

class ZipfianGenerator : public Generator<uint64_t> {
 public:
  constexpr static const double kZipfianConst = 0.99;
  static const uint64_t kMaxNumItems = (UINT64_MAX >> 24);

  ZipfianGenerator(uint64_t min, uint64_t max,
                   double zipfian_const = kZipfianConst) :
      num_items_(max - min + 1),
      base_(min),
      theta_(zipfian_const),
      zeta_n_(0),
      eta_(0),
      alpha_(0),
      zeta_2_(0),
      n_for_zeta_(0),
      last_value_(0),
      rng_(Seed()),
      uniform_(0.0, 1.0) 
  {
    assert(num_items_ >= 2 && num_items_ < kMaxNumItems);

    zeta_2_ = Zeta(2, theta_);
    alpha_ = 1.0 / (1.0 - theta_);

    RaiseZeta(num_items_);
    eta_ = Eta();

    Next();   // warm up
  }

  ZipfianGenerator(uint64_t num_items)
      : ZipfianGenerator(0, num_items - 1, kZipfianConst) { }

  uint64_t Next(uint64_t num_items);
  uint64_t Next() override { return Next(num_items_); }
  uint64_t Last() override { return last_value_; }

 private:

  // Strong per-thread seed
  static uint64_t Seed() {
    std::random_device rd;
    uint64_t t = std::chrono::high_resolution_clock::now()
                   .time_since_epoch().count();
    uint64_t tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return rd() ^ (t + (tid << 1));
  }

  void RaiseZeta(uint64_t num) {
    assert(num >= n_for_zeta_);
    zeta_n_ = Zeta(n_for_zeta_, num, theta_, zeta_n_);
    n_for_zeta_ = num;
  }

  double Eta() {
    return (1 - std::pow(2.0 / num_items_, 1 - theta_)) /
           (1 - zeta_2_ / zeta_n_);
  }

  static double Zeta(uint64_t last_num, uint64_t cur_num,
                     double theta, double last_zeta) {
    double z = last_zeta;
    for (uint64_t i = last_num + 1; i <= cur_num; ++i) {
      z += 1.0 / std::pow(i, theta);
    }
    return z;
  }

  static double Zeta(uint64_t num, double theta) {
    return Zeta(0, num, theta, 0);
  }

  uint64_t num_items_;
  uint64_t base_;
  
  double theta_;
  double zeta_n_;
  double eta_;
  double alpha_;
  double zeta_2_;
  uint64_t n_for_zeta_;
  uint64_t last_value_;

  // NEW: per-object RNG (thread-safe)
  std::mt19937_64 rng_;
  std::uniform_real_distribution<double> uniform_;

};

inline uint64_t ZipfianGenerator::Next(uint64_t num) {
  assert(num >= 2 && num < kMaxNumItems);

  if (num > n_for_zeta_) {
    RaiseZeta(num);
    eta_ = Eta();
  }

  double u = uniform_(rng_);      // FAST — no global RNG state
  double uz = u * zeta_n_;

  if (uz < 1.0) {
    return last_value_ = 0;
  }
  
  if (uz < 1.0 + std::pow(0.5, theta_)) {
    return last_value_ = 1;
  }

  return last_value_ =
      base_ + num * std::pow(eta_ * u - eta_ + 1, alpha_);
}

}

#endif // YCSB_C_ZIPFIAN_GENERATOR_H_
