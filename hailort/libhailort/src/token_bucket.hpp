// Note:
// * This module is taken from Facebook's open source Folly library: https://github.com/facebook/folly (v2020.08.17.00)
// * Changes:
//   * Changes made to the module are delimited with "BEGIN/END HAILO CHANGES"
//   * The file has been renamed from "TokenBucket.h" to "token_bucket.hpp"
//   * Removed:
//     * folly namespace
//     * BasicTokenBucket
//     * From BasicDynamicTokenBucket:
//       * Copy ctor and assignment operator
//       * available()
//       * reset()
//   * Original file: https://github.com/facebook/folly/blob/v2020.08.17.00/folly/TokenBucket.h
// * Copyright notices follow.

/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TOKEN_BUCKET_HPP_
#define TOKEN_BUCKET_HPP_

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

// BEGIN HAILO CHANGES
#include <hailo/hailort.h>
#include "hailo/expected.hpp"
#include "os/microsec_timer.hpp"

namespace hailort
{
// END HAILO CHANGES

/**
 * Thread-safe (atomic) token bucket implementation.
 *
 * A token bucket (http://en.wikipedia.org/wiki/Token_bucket) models a stream
 * of events with an average rate and some amount of burstiness. The canonical
 * example is a packet switched network: the network can accept some number of
 * bytes per second and the bytes come in finite packets (bursts). A token
 * bucket stores up to a fixed number of tokens (the burst size). Some number
 * of tokens are removed when an event occurs. The tokens are replenished at a
 * fixed rate. Failure to allocate tokens implies resource is unavailable and
 * caller needs to implement its own retry mechanism. For simple cases where
 * caller is okay with a FIFO starvation-free scheduling behavior, there are
 * also APIs to 'borrow' from the future effectively assigning a start time to
 * the caller when it should proceed with using the resource. It is also
 * possible to 'return' previously allocated tokens to make them available to
 * other users. Returns in excess of burstSize are considered expired and
 * will not be available to later callers.
 *
 * This implementation records the last time it was updated. This allows the
 * token bucket to add tokens "just in time" when tokens are requested.
 *
 * The "dynamic" base variant allows the token generation rate and maximum
 * burst size to change with every token consumption.
 *
 * @tparam Clock Clock type, must be steady i.e. monotonic.
 */
template <typename Clock = std::chrono::steady_clock>
class BasicDynamicTokenBucket {
  static_assert(Clock::is_steady, "clock must be steady");

 public:
  /**
   * Constructor.
   *
   * @param zeroTime Initial time at which to consider the token bucket
   *                 starting to fill. Defaults to 0, so by default token
   *                 buckets are "full" after construction.
   */
  explicit BasicDynamicTokenBucket(double zeroTime = 0) noexcept
      : zeroTime_(zeroTime) {}

  BasicDynamicTokenBucket(const BasicDynamicTokenBucket&) = delete;
  BasicDynamicTokenBucket& operator=(const BasicDynamicTokenBucket&) = delete;

  // BEGIN HAILO CHANGES
  BasicDynamicTokenBucket(BasicDynamicTokenBucket&& other) :
      zeroTime_(other.zeroTime_.load())
  {}
  // END HAILO CHANGES


  /**
   * Returns the current time in seconds since Epoch.
   */
  static double defaultClockNow() noexcept {
    auto const now = Clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
  }

  /**
   * Attempts to consume some number of tokens. Tokens are first added to the
   * bucket based on the time elapsed since the last attempt to consume tokens.
   * Note: Attempts to consume more tokens than the burst size will always
   * fail.
   *
   * Thread-safe.
   *
   * @param toConsume The number of tokens to consume.
   * @param rate Number of tokens to generate per second.
   * @param burstSize Maximum burst size. Must be greater than 0.
   * @param nowInSeconds Current time in seconds. Should be monotonically
   *                     increasing from the nowInSeconds specified in
   *                     this token bucket's constructor.
   * @return True if the rate limit check passed, false otherwise.
   */
  bool consume(
      double toConsume,
      double rate,
      double burstSize,
      double nowInSeconds = defaultClockNow()) {
    assert(rate > 0);
    assert(burstSize > 0);

    if (nowInSeconds <= zeroTime_.load()) {
      return 0;
    }

    return consumeImpl(
        rate, burstSize, nowInSeconds, [toConsume](double& tokens) {
          if (tokens < toConsume) {
            return false;
          }
          tokens -= toConsume;
          return true;
        });
  }

  /**
   * Similar to consume, but always consumes some number of tokens.  If the
   * bucket contains enough tokens - consumes toConsume tokens.  Otherwise the
   * bucket is drained.
   *
   * Thread-safe.
   *
   * @param toConsume The number of tokens to consume.
   * @param rate Number of tokens to generate per second.
   * @param burstSize Maximum burst size. Must be greater than 0.
   * @param nowInSeconds Current time in seconds. Should be monotonically
   *                     increasing from the nowInSeconds specified in
   *                     this token bucket's constructor.
   * @return number of tokens that were consumed.
   */
  double consumeOrDrain(
      double toConsume,
      double rate,
      double burstSize,
      double nowInSeconds = defaultClockNow()) {
    assert(rate > 0);
    assert(burstSize > 0);

    if (nowInSeconds <= zeroTime_.load()) {
      return 0;
    }

    double consumed;
    consumeImpl(
        rate, burstSize, nowInSeconds, [&consumed, toConsume](double& tokens) {
          if (tokens < toConsume) {
            consumed = tokens;
            tokens = 0.0;
          } else {
            consumed = toConsume;
            tokens -= toConsume;
          }
          return true;
        });
    return consumed;
  }

  /**
   * Return extra tokens back to the bucket. This will move the zeroTime_
   * value back based on the rate.
   *
   * Thread-safe.
   */
  void returnTokens(double tokensToReturn, double rate) {
    assert(rate > 0);
    assert(tokensToReturn > 0);

    returnTokensImpl(tokensToReturn, rate);
  }

  // BEGIN HAILO CHANGES
  /**
   * Like consumeOrDrain but the call will always satisfy the asked for count.
   * It does so by borrowing tokens from the future (zeroTime_ will move
   * forward) if the currently available count isn't sufficient.
   *
   * Returns a Expected<double>. The Expected wont be set if the request
   * cannot be satisfied: only case is when it is larger than burstSize. The
   * value of the Expected is a double indicating the time in seconds that the
   * caller needs to wait at which the reservation becomes valid. The caller
   * could simply sleep for the returned duration to smooth out the allocation
   * to match the rate limiter or do some other computation in the meantime. In
   * any case, any regular consume or consumeOrDrain calls will fail to allocate
   * any tokens until the future time is reached.
   *
   * Note: It is assumed the caller will not ask for a very large count nor use
   * it immediately (if not waiting inline) as that would break the burst
   * prevention the limiter is meant to be used for.
   *
   * Thread-safe.
   */
  Expected<double> consumeWithBorrowNonBlocking(
      double toConsume,
      double rate,
      double burstSize,
      double nowInSeconds = defaultClockNow()) {
    assert(rate > 0);
    assert(burstSize > 0);

    if (burstSize < toConsume) {
      return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    while (toConsume > 0) {
      double consumed =
          consumeOrDrain(toConsume, rate, burstSize, nowInSeconds);
      if (consumed > 0) {
        toConsume -= consumed;
      } else {
        double zeroTimeNew = returnTokensImpl(-toConsume, rate);
        double napTime = std::max(0.0, zeroTimeNew - nowInSeconds);
        return napTime;
      }
    }
    return 0;
  }

  /**
   * Convenience wrapper around non-blocking borrow to sleep inline until
   * reservation is valid.
   */
  bool consumeWithBorrowAndWait(
      double toConsume,
      double rate,
      double burstSize,
      double nowInSeconds = defaultClockNow()) {
    auto res = consumeWithBorrowNonBlocking(toConsume, rate, burstSize, nowInSeconds);
    if (!res.has_value()) {
      return false;
    }
    if (res.value() > 0) {
      MicrosecTimer::sleep(static_cast<uint64_t>(res.value() * 1000000));
    }
    return true;
  }
  // END HAILO CHANGES

 private:
  template <typename TCallback>
  bool consumeImpl(
      double rate,
      double burstSize,
      double nowInSeconds,
      const TCallback& callback) {
    auto zeroTimeOld = zeroTime_.load();
    double zeroTimeNew;
    do {
      auto tokens = std::min((nowInSeconds - zeroTimeOld) * rate, burstSize);
      if (!callback(tokens)) {
        return false;
      }
      zeroTimeNew = nowInSeconds - tokens / rate;
    } while (!zeroTime_.compare_exchange_weak(zeroTimeOld, zeroTimeNew));

    return true;
  }

  /**
   * Adjust zeroTime based on rate and tokenCount and return the new value of
   * zeroTime_. Note: Token count can be negative to move the zeroTime_ value
   * into the future.
   */
  double returnTokensImpl(double tokenCount, double rate) {
    auto zeroTimeOld = zeroTime_.load();
    double zeroTimeNew;
    do {
      zeroTimeNew = zeroTimeOld - tokenCount / rate;
    } while (!zeroTime_.compare_exchange_weak(zeroTimeOld, zeroTimeNew));
    return zeroTimeNew;
  }

  std::atomic<double> zeroTime_;
};

using DynamicTokenBucket = BasicDynamicTokenBucket<>;

} /* namespace hailort */

#endif /* TOKEN_BUCKET_HPP_ */
