#pragma once

#include <IPAddress.h>
#include <cstring>
#include <lwip/ip_addr.h>

#if LWIP_IPV6
#include <lwip/ip6_addr.h>
#include <lwip/ip6_zone.h>
#endif

class IPv6Address {
public:
    IPv6Address() {
        std::memset(_bytes, 0, sizeof(_bytes));
        _zone = 0;
    }

    explicit IPv6Address(const uint8_t *address, uint8_t zone = 0) {
        assign(address);
        _zone = zone;
    }

    explicit IPv6Address(const uint32_t address[4], uint8_t zone = 0) {
        assign(reinterpret_cast<const uint8_t *>(address));
        _zone = zone;
    }

#if LWIP_IPV6
    explicit IPv6Address(const ip6_addr_t &address) {
        assign(reinterpret_cast<const uint8_t *>(address.addr));
        _zone = ip6_addr_zone(&address);
    }
#endif

    explicit IPv6Address(const IPAddress &address) {
        if (address.type() == IPv6) {
            ip_addr_t ip_addr;
            address.to_ip_addr_t(&ip_addr);
            assign(reinterpret_cast<const uint8_t *>(ip_addr.u_addr.ip6.addr));
#if LWIP_IPV6
            _zone = ip_addr.u_addr.ip6.zone;
#else
            _zone = 0;
#endif
        } else {
            std::memset(_bytes, 0, sizeof(_bytes));
            _zone = 0;
        }
    }

    IPv6Address &operator=(const IPAddress &address) {
        *this = IPv6Address(address);
        return *this;
    }

    IPv6Address &operator=(const uint8_t *address) {
        assign(address);
        return *this;
    }

    IPv6Address &operator=(const uint32_t address[4]) {
        assign(reinterpret_cast<const uint8_t *>(address));
        return *this;
    }

#if LWIP_IPV6
    IPv6Address &operator=(const ip6_addr_t &address) {
        assign(reinterpret_cast<const uint8_t *>(address.addr));
        _zone = ip6_addr_zone(&address);
        return *this;
    }
#endif

    operator IPAddress() const {
        return IPAddress(IPv6, _bytes, _zone);
    }

    operator const uint8_t *() const {
        return _bytes;
    }

    operator const uint32_t *() const {
        return reinterpret_cast<const uint32_t *>(_bytes);
    }

    const uint8_t *raw() const {
        return _bytes;
    }

    uint8_t zone() const {
        return _zone;
    }

private:
    void assign(const uint8_t *address) {
        if (address) {
            std::memcpy(_bytes, address, sizeof(_bytes));
        } else {
            std::memset(_bytes, 0, sizeof(_bytes));
        }
    }

    uint8_t _bytes[16];
    uint8_t _zone;
};
