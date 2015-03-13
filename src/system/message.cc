#include "system/message.h"

namespace PS {

Message::Message(const NodeID& dest, int time, int wait_time)
    : recver(dest) {
  task.set_time(time);
  if (wait_time != kInvalidTime) task.add_wait_time(wait_time);
}

void Message::miniCopyFrom(const Message& msg) {
  task = msg.task;
  // task.clear_value_type();
  task.clear_has_key();
  terminate = msg.terminate;
  recv_handle = msg.recv_handle;
  fin_handle = msg.fin_handle;
  original_recver = msg.original_recver;
}

FilterConfig* Message::addFilter(FilterConfig::Type type) {
  auto ptr = task.add_filter();
  ptr->set_type(type);
  return ptr;
}

size_t Message::memSize() {
  size_t nbytes = task.SpaceUsed() + key.memSize();
  for (const auto& v : value) nbytes += v.memSize();
  return nbytes;
}

std::string Message::shortDebugString() const {
  std::stringstream ss;
  // if (task.request()) ss << "REQ"; else ss << "RLY";
  // ss << " T=" << task.time() << " ";
  // for (int i = 0; i < task.wait_time_size(); ++i) {
  //   if (i == 0) ss << "(wait";
  //   ss << " " << task.wait_time(i);
  //   if (i == task.wait_time_size() - 1) ss << ") ";
  // }
  // ss << sender << "=>" << recver << " ";
  // ss << (sender.empty() ? "I" : sender) << " --> " << recver << " ";
  if (!original_recver.empty()) ss << "(" << original_recver << ") ";
  if (key.size()) ss << "key [" << key.size() << "] ";
  if (value.size()) {
    ss << "value [";
    for (int i = 0; i < value.size(); ++i) {
      ss << value[i].size();
      if (i < value.size() - 1) ss << ",";
    }
    ss << "] ";
  }
  auto t = task; t.clear_msg(); ss << t.ShortDebugString();
  return ss.str();
}

std::string Message::debugString() const {
  std::stringstream ss;
  ss << "[message]: " << sender << "=>" << recver
     << "(" << original_recver << ")\n"
     << "[task]:" << task.ShortDebugString()
     << "\n[key]:" << key.size()
     << "\n[" << value.size() << " value]: ";
  for (const auto& x: value)
    ss << x.size() << " ";
  return ss.str();
}


} // namespace PS
