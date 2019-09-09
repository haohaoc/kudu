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
// under the License.

package org.apache.kudu.ranger;

import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class Wrapper {
    // Maximum number of threads in thread pool
    static final int MAX_T = 20;

  public static void main(String[] args) throws Exception {
    BlockingQueue<String> blockingQueue = new ArrayBlockingQueue<>(1000);
    MessageReader reader = new MessageReader(blockingQueue);
    Thread readerThread = new Thread(reader);
    readerThread.start();

    MessageSender sender = new MessageSender(blockingQueue);
    ExecutorService executorService = Executors.newFixedThreadPool(MAX_T);
    for (int i = 0; i < MAX_T * 50; i++) {
      executorService.submit(sender);
    }

    readerThread.join();
    sender.finish();
    executorService.shutdown();
  }
}
