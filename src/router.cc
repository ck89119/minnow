#include "router.hh"

#include <iostream>
#include <limits>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";
  route_table_[prefix_length].emplace_back(Entry { route_prefix, prefix_length, next_hop, interface_num });
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  for ( const auto& interface: _interfaces ) {
    auto &q = interface->datagrams_received();
    while (!q.empty()) {
      auto &dgram = q.front();
      if (dgram.header.ttl <= 1) {
        q.pop();
        continue;
      }

      auto dst_ip = dgram.header.dst;
      dgram.header.ttl -= 1;
      dgram.header.compute_checksum();

      auto find = false;
      for (int i = 32; i >= 1 && !find; --i ) {
        auto shift = 32 - i;
        auto gate= dst_ip >> shift << shift;

        for ( const auto& entry: route_table_[i] ) {
          if (entry.route_prefix == gate) {
            auto out_interface = _interfaces[entry.interface_num];
            auto &next_hop_addr = entry.next_hop.has_value()
                                    ? entry.next_hop.value()
                                    : Address::from_ipv4_numeric(dst_ip);
            out_interface->send_datagram(dgram, next_hop_addr);
            find = true;
          }
        }
      }

      if (!find) {
        // prefix_length = 0 match all
        for ( const auto& entry: route_table_[0] ) {
          auto out_interface = _interfaces[entry.interface_num];
          auto &next_hop_addr = entry.next_hop.has_value()
                                ? entry.next_hop.value()
                                : Address::from_ipv4_numeric(dst_ip);
          out_interface->send_datagram(dgram, next_hop_addr);
        }
      }

      q.pop();
    }
  }
}
