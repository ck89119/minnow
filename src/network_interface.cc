#include <iostream>

#include "arp_message.hh"
#include "exception.hh"
#include "network_interface.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  auto ipv4 = next_hop.ipv4_numeric();
  auto it = mp_.find(ipv4);
  if (it != mp_.end()) {
    send_datagram(dgram, it->second.first);
    return;
  }

  // sent arp request
  if (unacked_arp_.find(ipv4) == unacked_arp_.end()) {
    send_arp_message(ARPMessage::OPCODE_REQUEST, ETHERNET_BROADCAST, ipv4);
    unacked_arp_[ipv4] = age_ + ARP_REQUEST_GAP;
    unacked_arp_list_.push_back(unacked_arp_.find(ipv4));
  }

  datagrams_waiting_[ipv4].emplace_back(dgram);
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  auto &header = frame.header;
  if (header.dst != ethernet_address_ && header.dst != ETHERNET_BROADCAST) {
    return;
  }

  if (header.type == EthernetHeader::TYPE_IPv4) {
    InternetDatagram dgram {};
    if (!parse(dgram, frame.payload)) {
      return;
    }

    datagrams_received_.push( dgram );
  } else {
    ARPMessage msg {};
    if (!parse(msg, frame.payload)) {
      return;
    }

    if (msg.opcode == ARPMessage::OPCODE_REQUEST && msg.target_ip_address == ip_address_.ipv4_numeric()) {
        send_arp_message(ARPMessage::OPCODE_REPLY, msg.sender_ethernet_address, msg.sender_ip_address);
    }

    mp_[msg.sender_ip_address] = make_pair(msg.sender_ethernet_address, age_ + ARP_MAPPING_DURATION );
    list_.push_back(mp_.find(msg.sender_ip_address));

    // sent queued msg
    for ( const auto& dgram : datagrams_waiting_[msg.sender_ip_address] ) {
      send_datagram(dgram, msg.sender_ethernet_address);
    }
    datagrams_waiting_.erase(msg.sender_ip_address);
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  age_ += ms_since_last_tick;
  while (!list_.empty()) {
    auto &head = list_.front();
    auto timeout = head->second.second;
    if (timeout > age_ ) {
      break;
    }

    mp_.erase(head);
    list_.pop_front();
  }

  while (!unacked_arp_list_.empty()) {
    auto &head = unacked_arp_list_.front();
    auto timeout = head->second;
    if (timeout > age_ ) {
      break;
    }

    unacked_arp_.erase(head);
    unacked_arp_list_.pop_front();
  }
}

void NetworkInterface::send_datagram( const InternetDatagram& dgram, const EthernetAddress& next_hop )
{
  EthernetHeader frame_header {next_hop, ethernet_address_, EthernetHeader::TYPE_IPv4};
  EthernetFrame frame {frame_header, serialize(dgram)};
  transmit(frame);
}

void NetworkInterface::send_arp_message( uint16_t type, const EthernetAddress& dst_eth_addr, uint32_t dst_ip_addr )
{
  auto msg = ARPMessage();
  msg.opcode = type;
  msg.sender_ethernet_address = ethernet_address_;
  msg.sender_ip_address = ip_address_.ipv4_numeric();
  if (dst_eth_addr != ETHERNET_BROADCAST) {
    msg.target_ethernet_address = dst_eth_addr;
  }
  msg.target_ip_address = dst_ip_addr;

  EthernetHeader frame_header {dst_eth_addr, ethernet_address_, EthernetHeader::TYPE_ARP};
  EthernetFrame reply_frame {frame_header, serialize( msg )};
  transmit(reply_frame);
}
