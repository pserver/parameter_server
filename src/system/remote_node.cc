#include "system/remote_node.h"
#include "system/customer.h"
#include "util/crc32c.h"
#include "util/shared_array_inl.h"
namespace PS {

Filter* RemoteNode::FindFilterOrCreate(const FilterConfig& conf) {
  int id = conf.type();
  auto it = filters.find(id);
  if (it == filters.end()) {
    filters[id] = Filter::create(conf);
    it = filters.find(id);
  }
  return it->second;
}

void RemoteNode::EncodeMessage(const MessagePtr& msg) {
  const auto& tk = msg->task;
  for (int i = 0; i < tk.filter_size(); ++i) {
    FindFilterOrCreate(tk.filter(i))->encode(msg);
  }
}
void RemoteNode::DecodeMessage(const MessagePtr& msg) {
  const auto& tk = msg->task;
  // a reverse order comparing to encode
  for (int i = tk.filter_size()-1; i >= 0; --i) {
    FindFilterOrCreate(tk.filter(i))->decode(msg);
  }
}

void RemoteNode::AddSubNode(RemoteNode* rnode) {
  CHECK_NOTNULL(rnode);
  // insert s into sub_nodes such as sub_nodes is still ordered
  int pos = 0;
  Range<Key> kr(rnode->rnode.key());
  while (pos < nodes.size()) {
    if (kr.inLeft(Range<Key>(nodes[pos]->rnode.key()))) {
      break;
    }
    ++ pos;
  }
  nodes.insert(nodes.begin() + pos, rnode);
  keys.insert(keys.begin() + pos, kr);
}

void RemoteNode::RemoveSubNode(RemoteNode* rnode) {
  size_t n = nodes.size();
  CHECK_EQ(n, keys.size());
  for (int i = 0; i < n; ++i) {
    if (nodes[i] == rnode) {
      nodes.erase(nodes.begin() + i);
      keys.erase(keys.begin() + i);
      return;
    }
  }
}

} // namespace PS
