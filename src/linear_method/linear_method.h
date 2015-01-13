#pragma once
#include "linear_method/proto/lm.pb.h"
#include "linear_method/loss.h"
#include "linear_method/penalty.h"
#include "system/app.h"
namespace PS {
namespace LM {

App* createApp(const string& name, const Config& conf);

// linear classification/regerssion
class LinearMethod {
 public:
  LinearMethod(const Config& conf) : conf_(conf) { }
  virtual ~LinearMethod() { }

 protected:
  Config conf_;
  // Timer total_timer_;
  // Timer busy_timer_;

};

} // namespace LM
} // namespace PS
