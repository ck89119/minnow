#include "tcp_sender.hh"
#include "tcp_config.hh"

using namespace std;

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return not_acked_count_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retransmissions_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  auto msg = make_empty_message();
  auto len = min(!synced + reader().bytes_buffered() + !finned, available_window_size());

  // if not synced and msg has space
  if (!synced && len > 0) {
    synced = true;
    msg.SYN = true;
    len -= 1;
  }

  while (reader().bytes_buffered() > 0 && len > 0) {
    if (msg.payload.size() == TCPConfig::MAX_PAYLOAD_SIZE) {
      send_msg(std::move(msg), transmit);
      msg = make_empty_message();
    }

    auto read_len= min(min(len, reader().bytes_buffered()), TCPConfig::MAX_PAYLOAD_SIZE);
    read(input_.reader(), read_len, msg.payload);
    len -= read_len;

  }

  // if reader is finished, send a FIN
  if (!finned && input_.reader().is_finished() && len > 0) {
    finned = true;
    msg.FIN = true;
    len -= 1;
  }

  send_msg(std::move(msg), transmit);
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  auto msg = TCPSenderMessage();
  msg.seqno = next_seqno();
  msg.RST = writer().has_error();
  return msg;
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if (msg.RST) {
    writer().set_error();
    return;
  }

  auto absolute_ackno= get_absolute_seqno(msg.ackno.has_value() ? msg.ackno.value() : isn_);
  if (absolute_ackno > get_absolute_seqno(next_seqno())) {
    return;
  }

  bool new_ack = false;
  for (auto it = not_acked_.begin(); it != not_acked_.end(); it = not_acked_.erase(it)) {
    if (it->first > absolute_ackno) {
      break;
    }

    new_ack = true;
    not_acked_count_ -= it->second.sequence_length();
  }

  if (new_ack)  {
    consecutive_retransmissions_ = 0;
    timer_ = not_acked_.empty() ? 0 : initial_RTO_ms_ << consecutive_retransmissions_;
  }

  if (absolute_ackno >= recvr_expect_absolute_seqno_ ) {
    recvr_expect_absolute_seqno_ = absolute_ackno;
    window_size_ = msg.window_size;
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if (timer_ == 0) {
    return;
  }

  if (timer_ <= ms_since_last_tick) {
    transmit(not_acked_.begin()->second);

    consecutive_retransmissions_ += window_size_ > 0;
    timer_ = initial_RTO_ms_ << consecutive_retransmissions_;
  } else {
    timer_ -= ms_since_last_tick;
  }
}

void TCPSender::send_msg(TCPSenderMessage&& msg, const TransmitFunction& transmit)
{
  auto msg_len = msg.sequence_length();
  if (msg_len == 0) {
    return;
  }

  transmit( msg );
  not_acked_[get_absolute_seqno(msg.seqno + msg_len)] = std::move(msg) ;
  not_acked_count_ += msg_len;
  // if no outstanding msgs, transmit immediately and set timer
  if (timer_ == 0) {
    timer_ = initial_RTO_ms_;
  }
}

uint64_t TCPSender::get_absolute_seqno( const Wrap32& seqno )
{
  auto checkpoint = (reader().bytes_popped() + synced) >> 32 << 32;
  return seqno.unwrap(isn_, checkpoint);
}

uint64_t TCPSender::available_window_size() const
{
  auto right_edge= recvr_expect_absolute_seqno_ + window_size_;
  auto allocated = synced + reader().bytes_popped() + finned;

  auto size = uint64_t(0);
  if (right_edge > allocated) {
    // if there is enough space
    size = right_edge - allocated;
  } else if (right_edge == allocated && window_size_ == 0) {
    // with window size of 0, if no space left but no space pre-allocated, then pre-allocated 1 unit
    size = 1;
  }
  return size;
}
