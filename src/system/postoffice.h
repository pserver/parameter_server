#pragma once
#include "util/common.h"
#include "system/message.h"
#include "system/yellow_pages.h"
#include "system/heartbeat_info.h"
#include "util/threadsafe_queue.h"
#include "dashboard.h"
namespace PS {


DECLARE_int32(num_servers);
DECLARE_int32(num_workers);
DECLARE_int32(num_unused);
DECLARE_int32(num_replicas);
DECLARE_string(node_file);
DECLARE_string(app);

class App;
class Postoffice {
 public:
  SINGLETON(Postoffice);
  ~Postoffice();

  // Run the system
  void run();

  // Queue a message into the sending buffer, which will be sent by the sending
  // thread.
  void queue(const MessagePtr& msg);

  // reply *task* from *recver* by *reply_msg*
  void reply(const NodeID& recver,
             const Task& task,
             const string& reply_msg = string());

  // reply message *msg* with protocal message *proto*
  template <class P>
  void replyProtocalMessage(const MessagePtr& msg,
                            const P& proto);

  // accessors and mutators
  YellowPages& yp() { return yellow_pages_; }
  Node& myNode() { return yellow_pages_.van().myNode(); }
  Node& scheduler() { return yellow_pages_.van().scheduler(); }

  // HeartbeatInfo& hb() { return heartbeat_info_; };
 private:
  DISALLOW_COPY_AND_ASSIGN(Postoffice);
  Postoffice();

  bool IamScheduler() { return myNode().role() == Node::SCHEDULER; }
  void manageNode(const Task& pt);
  void manageApp(const Task& pt);
  void send();
  void recv();

  // void addMyNode(const string& name, const Node& recver);

  // app info only available for the scheduler, check IamScheduler() before using
  AppConfig app_conf_;
  App* app_ = nullptr;

  std::mutex mutex_;
  bool done_ = false;

  std::promise<void> nodes_are_ready_;
  std::unique_ptr<std::thread> recving_;
  std::unique_ptr<std::thread> sending_;
  threadsafe_queue<MessagePtr> sending_queue_;

  // yp_ should stay behind sending_queue_ so it will be destroied earlier
  YellowPages yellow_pages_;

  // string printDashboardTitle();
  // string printHeartbeatReport(const string& node_id, const HeartbeatReport& report);
  // std::unique_ptr<std::thread> heartbeating_;
  // std::unique_ptr<std::thread> monitoring_;
  // // heartbeat thread function
  // void heartbeat();
  // // monitor thread function only used by scheduler
  // void monitor();
  // // heartbeat info for workers/servers
  // HeartbeatInfo heartbeat_info_;
  // Dashboard dashboard_;
};

} // namespace PS
