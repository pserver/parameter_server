#include "system/van.h"
#include <string.h>
#include <zmq.h>
#include "util/shared_array_inl.h"
#include "util/local_machine.h"
#include "system/manager.h"
#include "system/postoffice.h"
namespace PS {

DEFINE_string(my_node, "role:SCHEDULER,hostname:'127.0.0.1',port:8000,id:'H'", "my node");
DEFINE_string(scheduler, "role:SCHEDULER,hostname:'127.0.0.1',port:8000,id:'H'", "the scheduler node");
DEFINE_int32(bind_to, 0, "binding port");
DEFINE_int32(my_rank, -1, "my rank among MPI peers");
DEFINE_string(interface, "", "network interface");

DECLARE_int32(num_workers);
DECLARE_int32(num_servers);


Van::~Van() {
  Statistic();
  for (auto& it : senders_) zmq_close(it.second);
  zmq_close(receiver_);
  zmq_ctx_destroy(context_);
}

void Van::Init(char* argv0) {
  scheduler_ = ParseNode(FLAGS_scheduler);
  if (FLAGS_my_rank < 0) {
    my_node_ = ParseNode(FLAGS_my_node);
  } else {
    my_node_ = AssembleMyNode();
  }

  // the earliest time I can get my_node_.id(), so put the log setup here
  // before logging anything
  if (FLAGS_log_dir.empty()) FLAGS_log_dir = "/tmp";

  if (!dirExists(FLAGS_log_dir)) { createDir(FLAGS_log_dir); }

  // change the hostname in default log filename to node id
  string logfile = FLAGS_log_dir + "/" + string(basename(argv0))
                   + "." + my_node_.id() + ".log.";
  google::SetLogDestination(google::INFO, (logfile+"INFO.").c_str());
  google::SetLogDestination(google::WARNING, (logfile+"WARNING.").c_str());
  google::SetLogDestination(google::ERROR, (logfile+"ERROR.").c_str());
  google::SetLogDestination(google::FATAL, (logfile+"FATAL.").c_str());
  google::SetLogSymlink(google::INFO, "");
  google::SetLogSymlink(google::WARNING, "");
  google::SetLogSymlink(google::ERROR, "");
  google::SetLogSymlink(google::FATAL, "");
  FLAGS_logbuflevel = -1;

  LOG(INFO) << "I'm [" << my_node_.ShortDebugString() << "]";

  context_ = zmq_ctx_new();
  CHECK(context_ != NULL) << "create 0mq context failed";

  // one need to "sudo ulimit -n 65536" or edit /etc/security/limits.conf
  zmq_ctx_set(context_, ZMQ_MAX_SOCKETS, 65536);
  // zmq_ctx_set(context_, ZMQ_IO_THREADS, 4);

  Bind();
  // connect(my_node_);
  Connect(scheduler_);

  // setup monitor
  if (IsScheduler()) {
    CHECK(!zmq_socket_monitor(receiver_, "inproc://monitor", ZMQ_EVENT_ALL));
  } else {
    CHECK(!zmq_socket_monitor(
        senders_[scheduler_.id()], "inproc://monitor", ZMQ_EVENT_ALL));
  }
  monitor_thread_ = new std::thread(&Van::Monitor, this);
  monitor_thread_->detach();
}


void Van::Bind() {
  receiver_ = zmq_socket(context_, ZMQ_ROUTER);
  CHECK(receiver_ != NULL)
      << "create receiver socket failed: " << zmq_strerror(errno);
  string addr = "tcp://*:";
  if (FLAGS_bind_to) {
    addr += std::to_string(FLAGS_bind_to);
  } else {
    CHECK(my_node_.has_port()) << my_node_.ShortDebugString();
    addr += std::to_string(my_node_.port());
  }
  // addr = "ipc:///tmp/" + my_node_.id();
  CHECK(zmq_bind(receiver_, addr.c_str()) == 0)
      << "bind to " << addr << " failed: " << zmq_strerror(errno);

  VLOG(1) << "BIND address " << addr;
}

void Van::Disconnect(const Node& node) {
  CHECK(node.has_id()) << node.ShortDebugString();
  NodeID id = node.id();
  if (senders_.find(id) != senders_.end()) {
    zmq_close (senders_[id]);
  }
  senders_.erase(id);
  VLOG(1) << "DISCONNECT from " << node.id();
}

bool Van::Connect(const Node& node) {
  CHECK(node.has_id()) << node.ShortDebugString();
  CHECK(node.has_port()) << node.ShortDebugString();
  CHECK(node.has_hostname()) << node.ShortDebugString();
  NodeID id = node.id();
  if (id == my_node_.id()) {
    // update my node info
    my_node_ = node;
  }
  if (senders_.find(id) != senders_.end()) {
    return true;
  }
  void *sender = zmq_socket(context_, ZMQ_DEALER);
  CHECK(sender != NULL) << zmq_strerror(errno);
  string my_id = my_node_.id(); // address(my_node_);
  zmq_setsockopt (sender, ZMQ_IDENTITY, my_id.data(), my_id.size());

  // TODO is it useful?
  // uint64_t hwm = 5000000;
  // zmq_setsockopt (sender, ZMQ_SNDHWM, &hwm, sizeof(hwm));

  // connect
  string addr = "tcp://" + node.hostname() + ":" + std::to_string(node.port());
  // addr = "ipc:///tmp/" + node.id();
  if (zmq_connect(sender, addr.c_str()) != 0) {
    LOG(WARNING) << "connect to " + addr + " failed: " + zmq_strerror(errno);
    return false;
  }

  senders_[id] = sender;
  hostnames_[id] = node.hostname();

  VLOG(1) << "CONNECT to " << id << " [" << addr << "]";
  return true;
}

bool Van::Send(Message* msg, size_t* send_bytes) {
  // find the socket
  NodeID id = msg->recver;
  auto it = senders_.find(id);
  if (it == senders_.end()) {
    LOG(WARNING) << "there is no socket to node " + id;
    return false;
  }
  void *socket = it->second;

  // double check
  bool has_key = !msg->key.empty();
  if (has_key) {
    msg->task.set_has_key(has_key);
  } else {
    msg->task.clear_has_key();
  }
  int n = has_key + msg->value.size();

  // send task
  size_t data_size = 0;
  string str;
  CHECK(msg->task.SerializeToString(&str))
      << "failed to serialize " << msg->task.ShortDebugString();
  int tag = ZMQ_SNDMORE;
  if (n == 0) tag = 0; // ZMQ_DONTWAIT;
  while (true) {
    if (zmq_send(socket, str.c_str(), str.size(), tag) == str.size()) break;
    if (errno == EINTR) continue;  // may be interupted by google profiler
    LOG(WARNING) << "failed to send message to node [" << id
                 << "] errno: " << zmq_strerror(errno);
    return false;
  }
  data_size += str.size();

  // send data
  for (int i = 0; i < n; ++i) {
    const auto& raw = (has_key && i == 0) ? msg->key : msg->value[i-has_key];
    if (i == n - 1) tag = 0; // ZMQ_DONTWAIT;
    while (true) {
      if (zmq_send(socket, raw.data(), raw.size(), tag) == raw.size()) break;
      if (errno == EINTR) continue;  // may be interupted by google profiler
      LOG(WARNING) << "failed to send message to node [" << id
                   << "] errno: " << zmq_strerror(errno);
      return false;
    }
    data_size += raw.size();
  }

  // statistics
  *send_bytes += data_size;
  if (hostnames_[id] == my_node_.hostname()) {
    sent_to_local_ += data_size;
  } else {
    sent_to_others_ += data_size;
  }
  VLOG(1) << "TO " << msg->recver << " " << msg->ShortDebugString();
  return true;
}

bool Van::Recv(Message* msg, size_t* recv_bytes) {
  size_t data_size = 0;
  msg->clear_data();
  NodeID sender;
  for (int i = 0; ; ++i) {
    zmq_msg_t zmsg;
    CHECK(zmq_msg_init(&zmsg) == 0) << zmq_strerror(errno);
    while (true) {
      if (zmq_msg_recv(&zmsg, receiver_, 0) != -1) break;
      if (errno == EINTR) continue;  // may be interupted by google profiler
      LOG(WARNING) << "failed to receive message. errno: "
                   << zmq_strerror(errno);
      return false;
   }
    char* buf = (char *)zmq_msg_data(&zmsg);
    CHECK(buf != NULL);
    size_t size = zmq_msg_size(&zmsg);
    data_size += size;
    if (i == 0) {
      // identify
      sender = std::string(buf, size);
      msg->sender = sender;
      msg->recver = my_node_.id();
    } else if (i == 1) {
      // task
      CHECK(msg->task.ParseFromString(std::string(buf, size)))
          << "parse string from " << sender << " I'm " << my_node_.id() << " "
          << size;
      if (IsScheduler() && msg->task.control() &&
          msg->task.ctrl().cmd() == Control::REQUEST_APP) {
        // it is the first time the scheduler receive message from the
        // sender. store the file desciptor of the sender for the monitor
        int val[10]; size_t val_len = sender.size();
        CHECK_LT(val_len, 300);
        memcpy(val, sender.data(), val_len);
        CHECK(!zmq_getsockopt(receiver_,  ZMQ_IDENTITY_FD, (char*)val, &val_len))
            << "failed to get the file descriptor of " << sender;
        CHECK_EQ(val_len, 4);
        int fd = val[0];
        VLOG(1) << "node [" << sender << "] is on file descriptor " << fd;
        Lock l(fd_to_nodeid_mu_);
        fd_to_nodeid_[fd] = sender;
      }
    } else {
      // data
      SArray<char> data; data.CopyFrom(buf, size);
      if (i == 2 && msg->task.has_key()) {
        msg->key = data;
      } else {
        msg->value.push_back(data);
      }
    }
    zmq_msg_close(&zmsg);
    if (!zmq_msg_more(&zmsg)) { CHECK_GT(i, 0); break; }
  }

  *recv_bytes += data_size;
  if (hostnames_[sender] == my_node_.hostname()) {
    received_from_local_ += data_size;
  } else {
    received_from_others_ += data_size;
  }
  VLOG(1) << "FROM: " << msg->sender << " " << msg->ShortDebugString();
  return true;
}

void Van::Statistic() {
  // if (my_node_.role() == Node::UNUSED || my_node_.role() == Node::SCHEDULER) return;
  auto gb = [](size_t x) { return  x / 1e9; };
  LOG(INFO) << my_node_.id()
            << " sent " << gb(sent_to_local_ + sent_to_others_)
            << " (local " << gb(sent_to_local_) << ") Gbyte,"
            << " received " << gb(received_from_local_ + received_from_others_)
            << " (local " << gb(received_from_local_) << ") Gbyte";
}

Node Van::ParseNode(const string& node_str) {
  Node node; CHECK(
      google::protobuf::TextFormat::ParseFromString(node_str, &node));
  if (!node.has_id()) {
    node.set_id(node.hostname() + ":" + std::to_string(node.port()));
  }
  return node;
}

Node Van::AssembleMyNode() {
  if (0 == FLAGS_my_rank) {
    return scheduler_;
  }

  Node ret_node;
  // role and id
  if (FLAGS_my_rank <= FLAGS_num_workers) {
    ret_node.set_role(Node::WORKER);
    ret_node.set_id("W" + std::to_string(FLAGS_my_rank - 1));
  } else if (FLAGS_my_rank <= FLAGS_num_workers + FLAGS_num_servers) {
    ret_node.set_role(Node::SERVER);
    ret_node.set_id("S" + std::to_string(FLAGS_my_rank - FLAGS_num_workers - 1));
  } else {
    ret_node.set_role(Node::UNUSED);
    ret_node.set_id("U" + std::to_string(
      FLAGS_my_rank - FLAGS_num_workers - FLAGS_num_servers - 1));
  }

  // IP, port and interface
  string ip;
  string interface = FLAGS_interface;
  unsigned short port;

  if (interface.empty()) {
    LocalMachine::pickupAvailableInterfaceAndIP(interface, ip);
  } else {
    ip = LocalMachine::IP(interface);
  }
  CHECK(!ip.empty()) << "failed to got ip";
  CHECK(!interface.empty()) << "failed to got the interface";
  port = LocalMachine::pickupAvailablePort();
  CHECK_NE(port, 0) << "failed to get port";
  ret_node.set_hostname(ip);
  ret_node.set_port(static_cast<int32>(port));

  return ret_node;
}

void Van::Monitor() {
  VLOG(1) << "starting monitor...";
  void *s = CHECK_NOTNULL(zmq_socket (context_, ZMQ_PAIR));
  CHECK(!zmq_connect (s, "inproc://monitor"));
  while (true) {
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    if (zmq_msg_recv(&msg, s, 0) == -1) {
      if (errno == EINTR) continue;  // may be interupted by google profiler
      break;
    }
    uint8_t *data = (uint8_t *)zmq_msg_data (&msg);
    int event = *(uint16_t *)(data);
    int value = *(uint32_t *)(data + 2);

    if (event == ZMQ_EVENT_DISCONNECTED) {
      auto& manager = Postoffice::instance().manager();
      if (IsScheduler()) {
        Lock l(fd_to_nodeid_mu_);
        if (fd_to_nodeid_.find(value) == fd_to_nodeid_.end()) {
          LOG(WARNING) << "cannot find the node id for FD = " << value;
          continue;
        }
        manager.NodeDisconnected(fd_to_nodeid_[value]);
      } else {
        manager.NodeDisconnected(scheduler_.id());
      }
    }
    if (event == ZMQ_EVENT_MONITOR_STOPPED) break;
  }
  zmq_close (s);
  VLOG(1) << "monitor stopped.";
}

} // namespace PS

  // check whether I could connect to a specified node
  // bool connected(const Node& node);

// bool Van::connected(const Node& node) {
//   auto it = senders_.find(node.id());
//   return it != senders_.end();
// }
