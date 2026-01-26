#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

/**
 * Rate Limiter Module
 *
 * Implements denial rate limiting with exponential backoff for the approval gate system.
 * When a user repeatedly denies tool requests, this module enforces a backoff period
 * before allowing new prompts for that tool.
 *
 * This module is an opaque type that owns its own data - callers should not
 * access internal fields directly.
 */

/**
 * Opaque rate limiter type.
 * Use rate_limiter_create() to create and rate_limiter_destroy() to free.
 */
typedef struct RateLimiter RateLimiter;

/**
 * Create a new rate limiter.
 *
 * @return Newly allocated RateLimiter, or NULL on failure
 */
RateLimiter *rate_limiter_create(void);

/**
 * Destroy a rate limiter and free all resources.
 *
 * @param rl Rate limiter to destroy (can be NULL)
 */
void rate_limiter_destroy(RateLimiter *rl);

/**
 * Check if a key (typically tool name) is rate-limited.
 *
 * @param rl Rate limiter
 * @param key Key to check (typically tool name)
 * @return 1 if rate limited, 0 if not
 */
int rate_limiter_is_blocked(const RateLimiter *rl, const char *key);

/**
 * Record a denial for rate limiting.
 * Increments denial counter and calculates backoff period.
 *
 * @param rl Rate limiter
 * @param key Key to record denial for (typically tool name)
 */
void rate_limiter_record_denial(RateLimiter *rl, const char *key);

/**
 * Reset denial counter for a key (on approval or backoff expiry).
 *
 * @param rl Rate limiter
 * @param key Key to reset
 */
void rate_limiter_reset(RateLimiter *rl, const char *key);

/**
 * Get the remaining backoff time for a rate-limited key.
 *
 * @param rl Rate limiter
 * @param key Key to check
 * @return Seconds remaining, or 0 if not rate limited
 */
int rate_limiter_get_remaining(const RateLimiter *rl, const char *key);

#endif /* RATE_LIMITER_H */
