#ifndef NDNPH_PROGRAMS_CLI_COMMON_HPP
#define NDNPH_PROGRAMS_CLI_COMMON_HPP

#include <NDNph-config.h>
#define NDNPH_MEMIF_DEBUG
#define NDNPH_SOCKET_DEBUG
#include <NDNph.h>

#include <cinttypes>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <unistd.h>

namespace cli_common {
namespace detail {

inline ndnph::Face*
openMemif(const char* socketName)
{
#ifdef NDNPH_PORT_TRANSPORT_MEMIF
  static ndnph::StaticRegion<16384> rxRegion;
  static ndnph::MemifTransport transport(rxRegion);
  if (!transport.begin(socketName, 0)) {
    return nullptr;
  }
  static ndnph::Face face(transport);
  return &face;
#else
  (void)socketName;
  return nullptr;
#endif // NDNPH_PORT_TRANSPORT_MEMIF
}

inline ndnph::Face*
openUdp()
{
  sockaddr_in raddr = {};
  raddr.sin_family = AF_INET;
  raddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  raddr.sin_port = htons(6363);

  const char* env = getenv("NDNPH_UPLINK_UDP");
  if (env != nullptr && inet_aton(env, &raddr.sin_addr) == 0) {
    return nullptr;
  }

  static ndnph::UdpUnicastTransport transport(1500);
  if (!transport.beginTunnel(&raddr)) {
    return nullptr;
  }
  static ndnph::Face face(transport);
  return &face;
}

} // namespace detail

/** @brief Open uplink face. */
inline ndnph::Face&
openUplink()
{
  static ndnph::Face* face = nullptr;
  if (face == nullptr) {
    const char* envMemif = getenv("NDNPH_UPLINK_MEMIF");
    if (envMemif != nullptr) {
      face = detail::openMemif(envMemif);
    }
    if (face == nullptr) {
      face = detail::openUdp();
    }
    if (face == nullptr) {
      fprintf(stderr, "Unable to open uplink\n");
      exit(1);
    }
  }
  return *face;
}

/** @brief Open KeyChain according to `NDNPH_KEYCHAIN` environ. */
inline ndnph::KeyChain&
openKeyChain()
{
  static ndnph::KeyChain keyChain;
  static bool ready = false;
  if (!ready) {
    const char* env = getenv("NDNPH_KEYCHAIN");
    if (env == nullptr) {
      fprintf(stderr,
              "KeyChain path missing: set NDNPH_KEYCHAIN=/path/to/keychain environment variable\n");
      exit(1);
    }

    ready = keyChain.open(env);
    if (!ready) {
      fprintf(stderr, "KeyChain open error\n");
      exit(1);
    }
  }
  return keyChain;
}

/** @brief Check KeyChain object ID has the proper format. */
inline std::string
checkKeyChainId(const std::string& id)
{
  bool ok = std::all_of(id.begin(), id.end(), [](char ch) {
    return static_cast<bool>(std::islower(ch)) || static_cast<bool>(std::isdigit(ch));
  });
  if (id.empty() || !ok) {
    fprintf(
      stderr,
      "Bad KeyChain ID [%s]; must be non-empty and only contain digits and lower-case letters\n",
      id.data());
    exit(1);
  }
  return id;
}

/** @brief Load a key from the KeyChain. */
inline void
loadKey(ndnph::Region& region, const std::string& id, ndnph::EcPrivateKey& pvt,
        ndnph::EcPublicKey& pub)
{
  if (!ndnph::ec::load(openKeyChain(), id.data(), region, pvt, pub)) {
    fprintf(stderr, "Key [%s] not found in KeyChain\n", id.data());
    exit(1);
  }
}

/** @brief Load a certificate from the KeyChain. */
inline ndnph::Data
loadCertificate(ndnph::Region& region, const std::string& id)
{
  auto cert = openKeyChain().certs.get(id.data(), region);
  if (!cert) {
    fprintf(stderr, "Certificate [%s] not found in KeyChain\n", id.data());
    exit(1);
  }
  return cert;
}

/** @brief Load a certificate in binary format from stdin. */
inline ndnph::Data
inputCertificate(ndnph::Region& region, ndnph::EcPublicKey* pub)
{
  const size_t bufferSize = 4096;
  uint8_t* buffer = region.alloc(bufferSize);
  std::cin.read(reinterpret_cast<char*>(buffer), bufferSize);

  auto data = region.create<ndnph::Data>();
  if (!data || !ndnph::Decoder(buffer, std::cin.gcount()).decode(data) ||
      !(pub == nullptr ? ndnph::certificate::isCertificate(data) : pub->import(region, data))) {
    fprintf(stderr, "Input certificate decode error\n");
    exit(1);
  }
  return data;
}

/** @brief Write an object in binary format to stdout. */
template<typename Encodable>
inline void
output(const Encodable& packet)
{
  ndnph::StaticRegion<65536> temp;
  ndnph::Encoder encoder(temp);
  if (!encoder.prepend(packet)) {
    fprintf(stderr, "Encode error\n");
    exit(1);
  }
  std::cout.write(reinterpret_cast<const char*>(encoder.begin()), encoder.size());
}

} // namespace cli_common

#endif // NDNPH_PROGRAMS_CLI_COMMON_HPP
