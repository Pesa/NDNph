#ifndef NDNPH_TLV_ENCODER_HPP
#define NDNPH_TLV_ENCODER_HPP

#include "../core/region.hpp"
#include "varnum.hpp"

namespace ndnph {

/** @brief TLV encoder that accepts items in reverse order. */
class Encoder
{
public:
  /** @brief Create over given buffer. */
  explicit Encoder(uint8_t* buf, size_t capacity) { init(buf, capacity); }

  /**
   * @brief Create over remaining space in a Region.
   *
   * After encoding, unused space can be released with trim().
   */
  explicit Encoder(Region& region)
    : m_region(&region)
  {
    size_t capacity = region.available();
    init(region.alloc(capacity), capacity);
  }

  /** @brief Whether an error has occurred, such as running out of space. */
  bool operator!() const { return m_pos == nullptr; }

  /** @brief Get output begin. */
  const uint8_t* begin() const { return m_pos; }
  /** @brief Get output end. */
  const uint8_t* end() const { return m_pos == nullptr ? nullptr : m_end; }
  /** @brief Get output size. */
  size_t size() const { return m_pos == nullptr ? 0 : m_end - m_pos; }

  /**
   * @brief Release unused space to the Region.
   *
   * This function has no effect if Encoder was not created from a Region.
   */
  void trim() const
  {
    if (m_region == nullptr) {
      return;
    }
    m_region->free(m_buf, (m_pos == nullptr ? m_end : m_pos) - m_buf);
    const_cast<Encoder*>(this)->m_buf = m_pos;
  }

  /**
   * @brief Release all space to the Region.
   * @post Output is empty.
   *
   * This function has no effect if Encoder was not created from a Region.
   */
  void discard()
  {
    if (m_region == nullptr || m_buf == nullptr) {
      return;
    }
    m_region->free(m_buf, m_end - m_buf);
    m_buf = m_pos = m_end = nullptr;
  }

  /**
   * @brief Make room to prepend an object.
   * @return room to write object.
   * @retval nullptr no room available.
   */
  uint8_t* prependRoom(size_t size)
  {
    if (m_pos == nullptr || m_pos - m_buf < static_cast<ssize_t>(size)) {
      m_pos = nullptr;
    } else {
      m_pos -= size;
    }
    return m_pos;
  }

  /**
   * @brief Prepend TLV-TYPE and TLV-LENGTH.
   * @return whether success.
   */
  bool prependTypeLength(uint32_t type, size_t length)
  {
    size_t sizeT = tlv::sizeofVarNum(type);
    size_t sizeL = tlv::sizeofVarNum(length);
    uint8_t* room = prependRoom(sizeT + sizeL);
    if (room == nullptr) {
      return false;
    }
    tlv::writeVarNum(room, type);
    tlv::writeVarNum(room + sizeT, length);
    return true;
  }

  /**
   * @brief Prepend a sequence of values.
   * @tparam First either `[] (Encoder&) -> void {}` or a class with
   *               `void encoderTo(Encoder&) const {}` method.
   * @tparam Arg same as First.
   * @return whether success.
   */
  template<typename First, typename... Arg>
  bool prepend(const First& first, const Arg&... arg)
  {
    prepend(arg...);
    prependOne(first);
    return !!*this;
  }

  /** @brief Raw buffer to prepend. */
  class Value
  {
  public:
    explicit Value(const uint8_t* value, size_t length)
      : m_value(value)
      , m_length(length)
    {}

    void encodeTo(Encoder& encoder) const
    {
      uint8_t* room = encoder.prependRoom(m_length);
      if (room != nullptr) {
        std::copy_n(m_value, m_length, room);
      }
    }

  private:
    const uint8_t* m_value;
    size_t m_length;
  };

  enum OmitEmptyTag
  {
    NoOmitEmpty = 0,
    OmitEmpty = 1,
  };

  /**
   * @brief Prepend TLV, measuring TLV-LENGTH automatically.
   * @tparam Arg same as arguments of prepend().
   * @param type TLV-TYPE number.
   * @param omitEmpty if OmitEmpty, omit the TLV altogether if TLV-LENGTH is zero.
   * @param arg zero or more items in TLV-VALUE.
   * @return whether success.
   */
  template<typename... Arg>
  bool prependTlv(uint32_t type, OmitEmptyTag omitEmpty = NoOmitEmpty,
                  const Arg&... arg)
  {
    uint8_t* after = m_pos;
    bool ok = prepend(arg...);
    size_t length = after - m_pos;
    if (length == 0 && omitEmpty == OmitEmpty) {
      return ok;
    }
    return ok && prependTypeLength(type, length);
  }

  /** @brief Prepend TLV, measuring TLV-LENGTH automatically. */
  template<typename First, typename... Arg>
  typename std::enable_if<!std::is_same<First, OmitEmptyTag>::value, bool>::type
  prependTlv(uint32_t type, const First& first, const Arg&... arg)
  {
    return prependTlv(type, NoOmitEmpty, first, arg...);
  }

private:
  void init(uint8_t* buf, size_t capacity)
  {
    m_buf = buf;
    m_pos = m_end = buf + capacity;
  }

  bool prepend() { return true; }

  template<typename T>
  typename std::enable_if<
    std::is_convertible<T, std::function<void(Encoder&)>>::value>::type
  prependOne(const T& encodeFunc)
  {
    encodeFunc(*this);
  }

  template<typename T>
  typename std::enable_if<
    !std::is_convertible<T, std::function<void(Encoder&)>>::value>::type
  prependOne(const T& encodable)
  {
    encodable.encodeTo(*this);
  }

private:
  Region* m_region = nullptr;
  uint8_t* m_buf = nullptr;
  uint8_t* m_pos = nullptr;
  uint8_t* m_end = nullptr;
};

} // namespace ndnph

#endif // NDNPH_TLV_ENCODER_HPP