#include "reassembler.hh"

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  auto &writer = output_.writer();
  auto check_writer_close = [&]() {
    if (is_last_ && next_idx_ == last_idx_ ) {
      writer.close();
    }
  };

  if (writer.is_closed()) {
    return;
  }

  if (!is_last_ && is_last_substring) {
    is_last_ = true;
    last_idx_ = first_index + data.size();
  }

  if (data.empty()) {
    check_writer_close();
    return;
  }

  // already pushed
  if (next_idx_ >= first_index + data.size()) {
    check_writer_close();
    return;
  }

  // exceed max cap
  if (first_index >= next_idx_ + writer.available_capacity()) {
    check_writer_close();
    return;
  }

  // all_data[start_idx, end_idx)
  auto start_idx = max(next_idx_, first_index);
  auto end_idx = min(next_idx_ + writer.available_capacity(), first_index + data.size());

  // merge current pending_map
  auto lb = pending_map_.lower_bound(start_idx);
  if (lb != pending_map_.begin()) {
    auto it_pre = prev( lb );
    start_idx = max(start_idx, it_pre->first + it_pre->second.size());
  }
  // remove overlapped item
  for (auto it = lb; it != pending_map_.end() ; it = pending_map_.erase(it)) {
    if (it->first + it->second.size() > end_idx) {
      end_idx = min(end_idx, it->first);
      break;
    }

    pending_count_ -= it->second.size();
  }

  if (start_idx >= end_idx) {
    check_writer_close();
    return;
  }

  data = data.substr(start_idx - first_index, end_idx - start_idx);
  pending_count_ += data.size();
  pending_map_[start_idx] = std::move(data);

  // try to push
  for (auto it = pending_map_.begin(); it != pending_map_.end(); it = pending_map_.erase( it )) {
    auto idx = it->first;
    if (idx > next_idx_) {
      break;
    }

    auto val = it->second;
    pending_count_ -= val.size();
    next_idx_ += val.size();
    writer.push(std::move(val));
  }

  check_writer_close();
}

uint64_t Reassembler::bytes_pending() const
{
  return pending_count_;
}
