# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

#######################################
# java_proto
#######################################

PROTOBUF_GENERATE_CPP(
  JAVA_PROTO_SRCS JAVA_PROTO_HDRS JAVA_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES java.proto)

add_library(java_proto
  ${JAVA_PROTO_SRCS}
  ${JAVA_PROTO_HDRS})
target_link_libraries(java_proto
  protobuf)

#######################################
# java_wrapper
#######################################

add_library(java_wrapper
  java_wrapper.cc
)
target_link_libraries(java_wrapper
  gutil
  java_proto
  kudu_util
  ${KUDU_BASE_LIBS}
)

#######################################
# Unit tests
#######################################

SET_KUDU_TEST_LINK_LIBS(
  java_wrapper)
ADD_KUDU_TEST(java_wrapper-test)