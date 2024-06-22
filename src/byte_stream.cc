#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ), data_(std::queue<std::string>()), view_( std::string_view("")) {}

bool Writer::is_closed() const
{
  return closed_;
}

void Writer::push( string data )
{
  auto len = min( available_capacity(), data.size() );
  if (len == 0) {
    return;
  }
  size_ += len;
  pushed_ += len;

  if (len < data.size()) {
    // TODO use string_view
    data = data.substr(0, len);
  }

  data_.push(std::move(data));
  if (data_.size() == 1) {
    view_ = data_.front();
  }
}

void Writer::close()
{
  closed_ = true;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - size_;
}

uint64_t Writer::bytes_pushed() const
{
  return pushed_;
}

bool Reader::is_finished() const
{
  return closed_ && size_ == 0;
}

uint64_t Reader::bytes_popped() const
{
  return popped_;
}

string_view Reader::peek() const
{
  return view_;
}

void Reader::pop( uint64_t len )
{
  len = min( len, size_ );
  size_ -= len;
  popped_ += len;

  while (len > 0) {
    if (len >= view_.size()) {
      len -= view_.size();
      data_.pop();
      view_ = data_.front();
      continue;
    }

    view_.remove_prefix(len);
    break;
  }
}

uint64_t Reader::bytes_buffered() const
{
  return size_;
}
