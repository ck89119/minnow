#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
   return zero_point + n;
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  auto n = 1ULL << 32;
  auto high = (checkpoint >> 32);
  auto x = (uint64_t(raw_value_) + n - zero_point.raw_value_) % n;
  auto abs = [](uint64_t a, uint64_t b) {
    return a > b ? a - b : b - a;
  };

  auto a = ((high - 1) << 32) + x;
  auto b = (high << 32) + x;
  auto c = ((high + 1) << 32) + x;

  auto ans = a;
  auto diff = abs(a, checkpoint);
  if (abs(b, checkpoint) < diff) {
    ans = b;
    diff = abs(b, checkpoint);
  }
  if (abs(c, checkpoint) < diff) {
    ans = c;
    diff = abs(c, checkpoint);
  }
  return ans;
}
