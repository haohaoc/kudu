// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License

#include <string>
#include <vector>

#include <glog/logging.h>

#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/java/java_wrapper.h"
#include "kudu/java/java.pb.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/env.h"
#include "kudu/util/path_util.h"
#include "kudu/util/status.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/subprocess.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/test_util.h"
#include "kudu/util/thread.h"

using std::string;
using std::unique_ptr;
using std::vector;
using strings::Substitute;

namespace kudu {
namespace java {

class JavaWrapperTest :
    public KuduTest,
    public ::testing::WithParamInterface<JavaWrapperProtocol::SerializationMode> {
 public:
  virtual void SetUp() override {
    string mode;
    switch (serde_mode()) {
      case JavaWrapperProtocol::SerializationMode::JSON: mode = "json"; break;
      case JavaWrapperProtocol::SerializationMode::PB: mode = "pb"; break;
      default: LOG(FATAL) << "Unknown serialization mode";
    }

    Env* env = Env::Default();
    string exe;
    ASSERT_OK(env->GetExecutablePath(&exe));
    string java_home;
    const string bin_dir = DirName(exe);
    ASSERT_OK(FindHomeDir("java", bin_dir, &java_home));
    shell_.reset(new Subprocess({
      Substitute("$0/bin/java", java_home),
      "-jar",
      "/Users/hao.hao/Documents/git-repos/kudu/java/kudu-ranger/build/libs/kudu-ranger-1.11.0-SNAPSHOT.jar"
    }));
    shell_->ShareParentStdin(false);
    shell_->ShareParentStdout(false);
    ASSERT_OK(shell_->Start());

    // Start the protocol interface.
    proto_.reset(new JavaWrapperProtocol(serde_mode(),
                                         JavaWrapperProtocol::CloseMode::CLOSE_ON_DESTROY,
                                         shell_->ReleaseChildStdoutFd(),
                                         shell_->ReleaseChildStdinFd()));
  }

  virtual void TearDown() override {
    if (proto_) {
      // Stopping the protocol interface will close the fds, causing the shell
      // to exit on its own.
      proto_.reset();
      ASSERT_OK(shell_->Wait());
      int exit_status;
      ASSERT_OK(shell_->GetExitStatus(&exit_status));
      ASSERT_EQ(0, exit_status);
    }
  }

  void RequestThread(const string& data, int loop) {
    for (int i = 0; i < loop; i++) {
      EchoRequestPB req;
      req.set_data(data);
      ASSERT_OK(proto_->SendMessage(req));
     }
  }

  void ResponseThread(int response_number) {
    // Parse the responses from Java subprocess.
    for (int i = 0; i < response_number; i++) {
      EchoResponsePB res;
      ASSERT_OK(proto_->ReceiveMessage(&res));
    }
  }

 protected:

  JavaWrapperProtocol::SerializationMode serde_mode() const {
    return GetParam();
  }

  unique_ptr<Subprocess> shell_;
  unique_ptr<JavaWrapperProtocol> proto_;
};

INSTANTIATE_TEST_CASE_P(SerializationModes, JavaWrapperTest,
                        ::testing::Values(
                            JavaWrapperProtocol::SerializationMode::PB));

TEST_P(JavaWrapperTest, TestJavaWrapperPB) {
  const int kNumSentAtOnce = 20;
  const int loop = 10000;
  string data = "This is a long long long payload";

  Stopwatch sw(Stopwatch::ALL_THREADS);
  sw.start();
  vector<scoped_refptr<Thread>> threads;
  for (int i = 0; i < kNumSentAtOnce; i++) {
    scoped_refptr<kudu::Thread> new_thread;
    ASSERT_OK(kudu::Thread::Create(
        "test", strings::Substitute("test-scanner-$0", i),
        &JavaWrapperTest::RequestThread, this, data, loop,
        &new_thread));
    threads.push_back(new_thread);
  }
  scoped_refptr<kudu::Thread> response_thread;
  ASSERT_OK(kudu::Thread::Create(
      "test", "test-response",
      &JavaWrapperTest::ResponseThread, this,
      kNumSentAtOnce * loop, &response_thread));

  SleepFor(MonoDelta::FromSeconds(1));

  for (const scoped_refptr<kudu::Thread> &thr : threads) {
    ASSERT_OK(ThreadJoiner(thr.get()).Join());
  }
  ASSERT_OK(ThreadJoiner(response_thread.get()).Join());

  sw.stop();

  float reqs_per_second = static_cast<float>(kNumSentAtOnce * loop / sw.elapsed().wall_seconds());
  LOG(INFO) << "Reqs/sec:         " << reqs_per_second;
  LOG(INFO) << "Mean:             " << static_cast<float>(sw.elapsed().wall_millis() / (kNumSentAtOnce * loop));
  LOG(INFO) << "Num. of reqs:     " << kNumSentAtOnce * loop;
  LOG(INFO) << "Ctx Sw. per req:  " << static_cast<float>(sw.elapsed().context_switches / (kNumSentAtOnce * loop));
}

} // namespace java
} // namespace kudu


