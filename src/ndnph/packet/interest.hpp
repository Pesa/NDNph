#ifndef NDNPH_PACKET_INTEREST_HPP
#define NDNPH_PACKET_INTEREST_HPP

#include "../core/in-region.hpp"
#include "../keychain/private-key.hpp"
#include "../keychain/public-key.hpp"
#include "../port/crypto/port.hpp"
#include "../port/random/port.hpp"
#include "convention.hpp"

namespace ndnph {
namespace detail {

/** @brief Fields in parameterized/signed Interest. */
struct InterestParams
{
  tlv::Value appParameters;
  ISigInfo sigInfo;
  tlv::Value sigValue;
  tlv::Value signedParams;
  tlv::Value allParams;
};

/** @brief Fields in Interest or Nack. */
class InterestObj : public InRegion
{
public:
  explicit InterestObj(Region& region)
    : InRegion(region)
    , canBePrefix(false)
    , mustBeFresh(false)
    , nackReason(0)
  {
    port::RandomSource::generate(reinterpret_cast<uint8_t*>(&nonce), sizeof(nonce));
  }

  enum
  {
    DefaultLifetime = 4000,
    MaxHopLimit = 0xFF,
  };

public:
  InterestParams* params = nullptr; // only relevant on a decoded packet
  Name name;
  uint32_t nonce = 0;
  uint16_t lifetime = DefaultLifetime;
  uint8_t hopLimit = MaxHopLimit;
  bool canBePrefix : 1;
  bool mustBeFresh : 1;
  uint8_t nackReason : 3;
};

class InterestRefBase : public RefRegion<InterestObj>
{
public:
  using RefRegion::RefRegion;

protected:
  ~InterestRefBase() = default;

  void encodeMiddle(Encoder& encoder) const
  {
    encoder.prepend(
      [this](Encoder& encoder) {
        if (obj->canBePrefix) {
          encoder.prependTlv(TT::CanBePrefix);
        }
      },
      [this](Encoder& encoder) {
        if (obj->mustBeFresh) {
          encoder.prependTlv(TT::MustBeFresh);
        }
      },
      [this](Encoder& encoder) { encoder.prependTlv(TT::Nonce, tlv::NNI4(obj->nonce)); },
      [this](Encoder& encoder) {
        if (obj->lifetime != InterestObj::DefaultLifetime) {
          encoder.prependTlv(TT::InterestLifetime, tlv::NNI(obj->lifetime));
        }
      },
      [this](Encoder& encoder) {
        if (obj->hopLimit != InterestObj::MaxHopLimit) {
          encoder.prependTlv(TT::HopLimit, tlv::NNI1(obj->hopLimit));
        }
      });
  }

  static int findParamsDigest(const Name& name)
  {
    int pos = -1;
    for (const auto& comp : name) {
      ++pos;
      if (comp.type() == TT::ParametersSha256DigestComponent) {
        return pos;
      }
    }
    return -1;
  }
};

class ParameterizedInterestRef : public InterestRefBase
{
public:
  explicit ParameterizedInterestRef(InterestObj* interest, tlv::Value appParameters)
    : InterestRefBase(interest)
    , m_appParameters(std::move(appParameters))
  {}

  void encodeTo(Encoder& encoder) const
  {
    encodeImpl(encoder, [this](Encoder& encoder) { encodeAppParameters(encoder); });
  }

protected:
  ~ParameterizedInterestRef() = default;

  void encodeName(Encoder& encoder, const tlv::Value& params) const
  {
    port::Sha256 hash;
    hash.update(params.begin(), params.size());
    uint8_t digestComp[34];
    if (!hash.final(&digestComp[2])) {
      encoder.setError();
      return;
    }
    digestComp[0] = TT::ParametersSha256DigestComponent;
    digestComp[1] = NDNPH_SHA256_LEN;

    tlv::Value prefix, suffix;
    int posParamsDigest = findParamsDigest(obj->name);
    if (posParamsDigest >= 0) {
      auto namePrefix = obj->name.slice(0, posParamsDigest);
      prefix = tlv::Value(namePrefix.value(), namePrefix.length());
      auto nameSuffix = obj->name.slice(posParamsDigest + 1);
      suffix = tlv::Value(nameSuffix.value(), nameSuffix.length());
    } else {
      prefix = tlv::Value(obj->name.value(), obj->name.length());
    }

    encoder.prependTlv(TT::Name, prefix, tlv::Value(digestComp, sizeof(digestComp)), suffix);
  }

  void encodeAppParameters(Encoder& encoder) const
  {
    encoder.prependTlv(TT::AppParameters, m_appParameters);
  }

  template<typename Fn>
  void encodeImpl(Encoder& encoder, const Fn& encodeParams) const
  {
    tlv::Value params;
    encoder.prependTlv(TT::Interest,
                       [this, &params](Encoder& encoder) { encodeName(encoder, params); },
                       [this](Encoder& encoder) { encodeMiddle(encoder); },
                       [&encodeParams, &params](Encoder& encoder) {
                         const uint8_t* paramsEnd = encoder.begin();
                         encodeParams(encoder);
                         if (!encoder) {
                           return;
                         }
                         const uint8_t* paramsBegin = encoder.begin();
                         params = tlv::Value(paramsBegin, paramsEnd - paramsBegin);
                       });
  }

protected:
  tlv::Value m_appParameters;
};

class SignedInterestRef : public ParameterizedInterestRef
{
public:
  explicit SignedInterestRef(InterestObj* interest, tlv::Value appParameters, const PrivateKey& key,
                             ISigInfo sigInfo)
    : ParameterizedInterestRef(interest, std::move(appParameters))
    , m_key(key)
    , m_sigInfo(std::move(sigInfo))
  {}

  void encodeTo(Encoder& encoder) const
  {
    tlv::Value signedName;
    int posParamsDigest = this->findParamsDigest(this->obj->name);
    if (posParamsDigest < 0) {
      signedName = tlv::Value(this->obj->name.value(), this->obj->name.length());
    } else if (static_cast<size_t>(posParamsDigest) == this->obj->name.size() - 1) {
      auto prefix = this->obj->name.getPrefix(-1);
      signedName = tlv::Value(prefix.value(), prefix.length());
    } else {
      encoder.setError();
      return;
    }

    m_key.updateSigInfo(m_sigInfo);
    uint8_t* after = const_cast<uint8_t*>(encoder.begin());
    uint8_t* sigBuf = encoder.prependRoom(m_key.getMaxSigLen());
    encoder.prepend([this](Encoder& encoder) { this->encodeAppParameters(encoder); }, m_sigInfo);
    if (!encoder) {
      return;
    }
    const uint8_t* signedPortion = encoder.begin();
    size_t sizeofSignedPortion = sigBuf - signedPortion;

    ssize_t sigLen =
      m_key.sign({ signedName, tlv::Value(signedPortion, sizeofSignedPortion) }, sigBuf);
    if (sigLen < 0) {
      encoder.setError();
      return;
    }
    if (static_cast<size_t>(sigLen) != m_key.getMaxSigLen()) {
      std::copy_backward(sigBuf, sigBuf + sigLen, after);
    }
    encoder.resetFront(after);

    this->encodeImpl(encoder, [this, sigLen](Encoder& encoder) {
      encoder.prepend([this](Encoder& encoder) { this->encodeAppParameters(encoder); }, m_sigInfo,
                      [sigLen](Encoder& encoder) {
                        encoder.prependRoom(sigLen); // room contains signature
                        encoder.prependTypeLength(TT::ISigValue, sigLen);
                      });
    });
  }

private:
  const PrivateKey& m_key;
  mutable ISigInfo m_sigInfo;
};

} // namespace detail

/** @brief Interest packet. */
class Interest : public detail::InterestRefBase
{
public:
  using InterestRefBase::InterestRefBase;

  const Name& getName() const
  {
    return obj->name;
  }

  void setName(const Name& v)
  {
    obj->name = v;
  }

  bool getCanBePrefix() const
  {
    return obj->canBePrefix;
  }

  void setCanBePrefix(bool v)
  {
    obj->canBePrefix = v;
  }

  bool getMustBeFresh() const
  {
    return obj->mustBeFresh;
  }

  void setMustBeFresh(bool v)
  {
    obj->mustBeFresh = v;
  }

  uint32_t getNonce() const
  {
    return obj->nonce;
  }

  void setNonce(uint32_t v)
  {
    obj->nonce = v;
  }

  uint16_t getLifetime() const
  {
    return obj->lifetime;
  }

  void setLifetime(uint16_t v)
  {
    obj->lifetime = v;
  }

  uint8_t getHopLimit() const
  {
    return obj->hopLimit;
  }

  void setHopLimit(uint8_t v)
  {
    obj->hopLimit = v;
  }

  /**
   * @brief Retrieve AppParameters.
   * @pre only available on decoded packet.
   * @note To create Interest packet with AppParameters, use parameterize().
   */
  tlv::Value getAppParameters() const
  {
    if (obj->params == nullptr) {
      return tlv::Value();
    }
    return obj->params->appParameters;
  }

  /**
   * @brief Retrieve SignatureInfo.
   * @pre only available on decoded packet.
   */
  const ISigInfo* getSigInfo() const
  {
    return obj->params == nullptr ? nullptr : &obj->params->sigInfo;
  }

  /** @brief Encode the Interest without AppParameters. */
  void encodeTo(Encoder& encoder) const
  {
    encoder.prependTlv(TT::Interest, obj->name,
                       [this](Encoder& encoder) { encodeMiddle(encoder); });
  }

  class Parameterized : public detail::ParameterizedInterestRef
  {
  public:
    using detail::ParameterizedInterestRef::ParameterizedInterestRef;

    detail::SignedInterestRef sign(const PrivateKey& key, ISigInfo sigInfo = ISigInfo()) const
    {
      return detail::SignedInterestRef(obj, this->m_appParameters, key, std::move(sigInfo));
    }
  };

  /**
   * @brief Add AppParameters to the packet.
   * @pre Name contains zero or one ParametersSha256DigestComponent.
   * @return an Encodable object, with an additional `sign(const PrivateKey&)` method
   *         to create a signed Interest. This object is valid only if Interest and
   *         appParameters are kept alive. It's recommended to pass it to Encoder
   *         immediately without saving as variable.
   * @note Unrecognized fields found during decoding are not preserved in encoding output.
   * @note This method does not set sigValue. Packet is not verifiable after this operation.
   */
  Parameterized parameterize(tlv::Value appParameters) const
  {
    return Parameterized(obj, std::move(appParameters));
  }

  /**
   * @brief Sign the packet with a private key.
   * @pre If Name contains ParametersSha256DigestComponent, it is the last component.
   * @return an Encodable object. This object is valid only if Interest and Key are kept alive.
   *         It's recommended to pass it to Encoder immediately without saving as variable.
   * @note Unrecognized fields found during decoding are not preserved in encoding output.
   * @note This method does not set sigValue. Packet is not verifiable after this operation.
   *
   * To create a signed Interest with AppParameters, call parameterize() first, then
   * call sign() on its return value.
   */
  detail::SignedInterestRef sign(const PrivateKey& key, ISigInfo sigInfo = ISigInfo()) const
  {
    return detail::SignedInterestRef(obj, tlv::Value(), key, std::move(sigInfo));
  }

  /** @brief Decode packet. */
  bool decodeFrom(const Decoder::Tlv& input)
  {
    return EvDecoder::decode(
      input, { TT::Interest }, EvDecoder::def<TT::Name>(&obj->name),
      EvDecoder::def<TT::CanBePrefix>([this](const Decoder::Tlv&) { setCanBePrefix(true); }),
      EvDecoder::def<TT::MustBeFresh>([this](const Decoder::Tlv&) { setMustBeFresh(true); }),
      EvDecoder::defNni<TT::Nonce, tlv::NNI4>(&obj->nonce),
      EvDecoder::defNni<TT::InterestLifetime, tlv::NNI>(&obj->lifetime),
      EvDecoder::defNni<TT::HopLimit, tlv::NNI1>(&obj->hopLimit),
      EvDecoder::def<TT::AppParameters>([this, &input](const Decoder::Tlv& d) {
        obj->params = regionOf(obj).template make<detail::InterestParams>();
        if (obj->params == nullptr) {
          return false;
        }
        obj->params->allParams = tlv::Value(d.tlv, input.tlv + input.size - d.tlv);
        return obj->params->appParameters.decodeFrom(d);
      }),
      EvDecoder::def<TT::ISigInfo>([this](const Decoder::Tlv& d) {
        return obj->params != nullptr && obj->params->sigInfo.decodeFrom(d);
      }),
      EvDecoder::def<TT::ISigValue>([this](const Decoder::Tlv& d) {
        if (obj->params == nullptr) {
          return false;
        }
        obj->params->signedParams =
          tlv::Value(obj->params->allParams.begin(), d.tlv - obj->params->allParams.begin());
        return obj->params->sigValue.decodeFrom(d);
      }));
  }

  /**
   * @brief Check ParametersSha256DigestComponent.
   * @return whether the digest is correct.
   *
   * This method only works on decoded packet.
   * It's unnecessary to call this method if you are going to use verify().
   */
  bool checkDigest() const
  {
    if (obj->params == nullptr) {
      return false;
    }
    int posParamsDigest = findParamsDigest(obj->name);
    if (posParamsDigest < 0) {
      return false;
    }
    Component paramsDigest = obj->name[posParamsDigest];

    uint8_t digest[NDNPH_SHA256_LEN];
    port::Sha256 hash;
    hash.update(obj->params->allParams.begin(), obj->params->allParams.size());
    return hash.final(digest) &&
           port::TimingSafeEqual()(digest, sizeof(digest), paramsDigest.value(),
                                   paramsDigest.length());
  }

  /**
   * @brief Verify the packet with a public key.
   * @return verification result.
   *
   * This method only works on decoded packet. It does not work on packet that
   * has been modified or (re-)signed.
   */
  bool verify(const PublicKey& key)
  {
    if (!checkDigest()) {
      return false;
    }
    int posParamsDigest = findParamsDigest(obj->name);
    if (static_cast<size_t>(posParamsDigest) != obj->name.size() - 1) {
      return false;
    }
    auto signedName = obj->name.getPrefix(-1);
    return key.verify(
      { tlv::Value(signedName.value(), signedName.length()), obj->params->signedParams },
      obj->params->sigValue.begin(), obj->params->sigValue.size());
  }

  /**
   * @brief Determine whether Data can satisfy Interest.
   *
   * This method only works reliably on decoded packets. For packets that are being constructed
   * or modified, this method may give incorrect results for parameterized/signed Interests or
   * Interest carrying implicit digest component.
   */
  template<typename DataT>
  bool match(const DataT& data) const
  {
    if (obj->mustBeFresh && data.getFreshnessPeriod() == 0) {
      return false;
    }
    const Name& dataName = data.getName();
    switch (obj->name.compare(dataName)) {
      case Name::CMP_EQUAL:
        return true;
      case Name::CMP_LPREFIX:
        return obj->canBePrefix;
      case Name::CMP_RPREFIX: {
        Component lastComp = obj->name[-1];
        uint8_t digest[NDNPH_SHA256_LEN];
        return obj->name.size() == dataName.size() + 1 &&
               lastComp.is<convention::ImplicitDigest>() && data.computeImplicitDigest(digest) &&
               port::TimingSafeEqual()(digest, sizeof(digest), lastComp.value(), lastComp.length());
      }
      default:
        return false;
    }
  }
};

} // namespace ndnph

#endif // NDNPH_PACKET_INTEREST_HPP
