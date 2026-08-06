#include <unity.h>

#include <set>

#include "portal_ap_password.h"
#include "portal_identity.h"

using namespace portal_ap_password;

namespace {

// Simple deterministic RNGs for tests. The pure generator only cares
// that its callback returns some uint32_t on each call.
uint32_t counterRng() {
  static uint32_t n = 0;
  return n++;
}

uint32_t constantRng() { return 42; }

bool alphabetContains(char c) {
  for (size_t i = 0; i < easyPasswordAlphabetSize(); ++i) {
    if (kEasyPasswordAlphabet[i] == c) return true;
  }
  return false;
}

}  // namespace

void test_easy_password_alphabet_excludes_confusables() {
  const String alpha(kEasyPasswordAlphabet);
  TEST_ASSERT_EQUAL(31u, easyPasswordAlphabetSize());
  // Explicit confusables that must NOT appear.
  const char banned[] = {'0', '1', 'i', 'l', 'o', 'I', 'L', 'O'};
  for (char c : banned) {
    for (size_t i = 0; i < alpha.length(); ++i) {
      TEST_ASSERT_NOT_EQUAL(c, alpha[i]);
    }
  }
}

void test_random_easy_password_returns_requested_length() {
  const String p = randomEasyPassword(8, counterRng);
  TEST_ASSERT_EQUAL(8u, p.length());
}

void test_random_easy_password_uses_only_alphabet_chars() {
  const String p = randomEasyPassword(32, counterRng);
  TEST_ASSERT_EQUAL(32u, p.length());
  for (size_t i = 0; i < p.length(); ++i) {
    TEST_ASSERT_TRUE_MESSAGE(alphabetContains(p[i]),
                             "password char not in alphabet");
  }
}

void test_random_easy_password_is_deterministic_with_fixed_rng() {
  // Same constant RNG -> same character every time.
  const String p = randomEasyPassword(8, constantRng);
  TEST_ASSERT_EQUAL(8u, p.length());
  for (size_t i = 1; i < p.length(); ++i) {
    TEST_ASSERT_EQUAL(p[0], p[i]);
  }
}

void test_random_easy_password_rejects_bad_input() {
  TEST_ASSERT_EQUAL(0u, randomEasyPassword(0, counterRng).length());
  TEST_ASSERT_EQUAL(0u, randomEasyPassword(64, counterRng).length());
  TEST_ASSERT_EQUAL(0u, randomEasyPassword(8, nullptr).length());
}

void test_mac_suffix_uses_last_two_bytes() {
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  char suffix[5] = {};
  portal_identity::formatSsidSuffix(mac, suffix);
  TEST_ASSERT_EQUAL_STRING("EEFF", suffix);
}
