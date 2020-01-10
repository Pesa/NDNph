#ifndef NDNPH_KEYCHAIN_DIGEST_KEY_HPP
#define NDNPH_KEYCHAIN_DIGEST_KEY_HPP

#include "../packet/sig-info.hpp"
#include "common.hpp"

namespace ndnph {

/**
 * @brief DigestSha256 signing and verification.
 * @tparam Sha256Port platform-specific SHA256 implementation.
 * @tparam TimingSafeEqual platform-specific timing safe equal implementation.
 */
template<typename Sha256Port, typename TimingSafeEqual = DefaultTimingSafeEqual>
class BasicDigestKey
{
public:
  void updateSigInfo(SigInfo& sigInfo) const
  {
    sigInfo.sigType = SigType::Sha256;
    sigInfo.name = Name();
  }

  using MaxSigLen = std::integral_constant<size_t, NDNPH_SHA256_LEN>;

  ssize_t sign(std::initializer_list<tlv::Value> chunks, uint8_t* sig) const
  {
    bool ok = detail::computeDigest<Sha256Port>(chunks, sig);
    return ok ? NDNPH_SHA256_LEN : -1;
  }

  bool verify(std::initializer_list<tlv::Value> chunks, const uint8_t* sig,
              size_t sigLen) const
  {
    uint8_t digest[NDNPH_SHA256_LEN];
    return detail::computeDigest<Sha256Port>(chunks, digest) &&
           TimingSafeEqual()(digest, NDNPH_SHA256_LEN, sig, sigLen);
  }
};

} // namespace ndnph

#endif // NDNPH_KEYCHAIN_DIGEST_KEY_HPP