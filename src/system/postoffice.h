#pragma once
#include "util/common.h"
#include "system/message.h"
#include "util/threadsafe_queue.h"
#include "system/manager.h"
#include "system/heartbeat_info.h"
namespace PS {

class Postoffice {
 public:
  SINGLETON(Postoffice);
  ~Postoffice();

  void Run(int* argc, char***);
  void Stop() { manager_.Stop(); }

  // Queue a message into the sending buffer, which will be sent by the sending
  // thread. It is thread safe.
  //
  // "msg" will be DELETE by system when "msg" is sent successfully. so do NOT
  // delete "msg" before
  void Queue(Message* msg) { sending_queue_.push(msg); }

  Manager& manager() { return manager_; }
  HeartbeatInfo& pm() { return perf_monitor_; }

 private:
  Postoffice();
  void Send();
  void Recv();

  std::unique_ptr<std::thread> recv_thread_;
  std::unique_ptr<std::thread> send_thread_;
  ThreadsafeQueue<Message*> sending_queue_;

  Manager manager_;
  HeartbeatInfo perf_monitor_;
  DISALLOW_COPY_AND_ASSIGN(Postoffice);
};

} // namespace PS
