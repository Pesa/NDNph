#ifndef NDNPH_TLV_VARNUM_HPP
#define NDNPH_TLV_VARNUM_HPP

#include "../core/common.hpp"

namespace ndnph {
namespace tlv {

/** @brief Compute size of VAR-NUMBER. */
constexpr size_t
sizeofVarNum(uint32_t n)
{
  return n < 0xFD ? 1 : n <= 0xFFFF ? 3 : 5;
}

/** @brief Write VAR-NUMBER. */
inline void
writeVarNum(uint8_t* room, uint32_t n)
{
  if (n < 0xFD) {
    room[0] = n;
  } else if (n <= 0xFFFF) {
    room[0] = 0xFD;
    room[1] = static_cast<uint8_t>(n >> 8);
    room[2] = static_cast<uint8_t>(n >> 0);
  } else {
    room[0] = 0xFE;
    room[1] = static_cast<uint8_t>(n >> 24);
    room[2] = static_cast<uint8_t>(n >> 16);
    room[3] = static_cast<uint8_t>(n >> 8);
    room[4] = static_cast<uint8_t>(n >> 0);
  }
}

/**
 * @brief Read VAR-NUMBER.
 * @return consumed bytes, or 0 upon error.
 */
inline int
readVarNum(const uint8_t* input, size_t size, uint32_t& n)
{
  if (size >= 1 && input[0] < 0xFD) {
    n = input[0];
    return 1;
  }
  if (size >= 3 && input[0] == 0xFD) {
    n = (input[1] << 8) | input[2];
    return 3;
  }
  if (size >= 5 && input[0] == 0xFE) {
    n = (input[1] << 24) | (input[2] << 16) | (input[3] << 8) | input[4];
    return 5;
  }
  return 0;
}

} // namespace tlv
} // namespace ndnph

#endif // NDNPH_TLV_VARNUM_HPP
