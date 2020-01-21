#ifndef NDNPH_FACE_BRIDGE_TRANSPORT_HPP
#define NDNPH_FACE_BRIDGE_TRANSPORT_HPP

#include "transport-rxqueue.hpp"

namespace ndnph {

/**
 * @brief Virtual transport that connects to an peer.
 * @tparam Queue a queue of RxQueueItem; it should be thread-safe if the two
 *               bridged transports are used in different threads.
 * @note A port is expected to typedef this template as `BridgeTransport` type.
 *
 * Queue capacity and MTU may be adjusted via constructor.
 */
template<typename Queue, typename QM = transport::DynamicRxQueueMixin<Queue>>
class BasicBridgeTransport
  : public virtual Transport
  , public QM
{
public:
  using BridgeTransport = BasicBridgeTransport<Queue>;

  using QM::QM;

  /**
   * @brief Connect to peer transport.
   * @post Packets sent on one transport are received at the other.
   */
  bool begin(BridgeTransport& peer)
  {
    if (m_peer != nullptr || peer.m_peer != nullptr) {
      return false;
    }
    m_peer = &peer;
    peer.m_peer = this;
    return true;
  }

  /** @brief Disconnect from peer transport. */
  bool end()
  {
    if (m_peer == nullptr || m_peer->m_peer != this) {
      return false;
    }
    m_peer->m_peer = nullptr;
    m_peer = nullptr;
    return true;
  }

private:
  bool doIsUp() const final
  {
    return m_peer != nullptr;
  }

  void doLoop() final
  {
    this->loopRxQueue();
  }

  bool doSend(const uint8_t* pkt, size_t pktLen, uint64_t endpointId) final
  {
    if (m_peer == nullptr) {
      return false;
    }
    if (auto r = m_peer->receiving()) {
      if (r.bufLen() < pktLen) {
        return false;
      }
      std::copy_n(pkt, pktLen, r.buf());
      r(pktLen, endpointId);
      return true;
    }
    return false;
  }

private:
  BridgeTransport* m_peer = nullptr;
};

} // namespace ndnph

#endif // NDNPH_FACE_BRIDGE_TRANSPORT_HPP
