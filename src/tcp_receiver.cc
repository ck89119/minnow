#include "tcp_receiver.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  if (message.RST) {
    byteStream().set_error();
    return;
  }

  if (message.SYN) {
    ISN = message.seqno;
  }

  if (!ISN.has_value()) {
    return;
  }

  auto checkpoint = writer().bytes_pushed() >> 32 << 32;
  auto stream_index = message.seqno.unwrap(ISN.value(), checkpoint) + message.SYN - 1;
  reassembler_.insert(stream_index, std::move(message.payload), message.FIN);
}

TCPReceiverMessage TCPReceiver::send() const
{
  TCPReceiverMessage message;
  if (writer().has_error()) {
    message.RST = true;
    return message;
  }

  if (ISN.has_value()) {
    message.ackno = std::optional<Wrap32>(ISN.value() + (writer().bytes_pushed() + 1 + writer().is_closed()));
  }
  message.window_size = min(writer().available_capacity(), uint64_t(UINT16_MAX));
  return message;
}
