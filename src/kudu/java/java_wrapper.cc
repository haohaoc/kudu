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

#include "kudu/java/java_wrapper.h"

#include <unistd.h>

#include <cerrno>
#include <string>

#include <google/protobuf/util/json_util.h>

#include "kudu/gutil/endian.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/java/java.pb.h"
#include "kudu/util/faststring.h"
#include "kudu/util/pb_util.h"
#include "kudu/util/status.h"

using std::string;
using strings::Substitute;

namespace kudu {
namespace java {

const int JavaWrapperProtocol::kMaxMessageBytes = 1024 * 1024;

JavaWrapperProtocol::JavaWrapperProtocol(SerializationMode serialization_mode,
                                         CloseMode close_mode,
                                         int read_fd,
                                         int write_fd)
  : serialization_mode_(serialization_mode),
    close_mode_(close_mode),
    read_fd_(read_fd),
    write_fd_(write_fd) {
}

JavaWrapperProtocol::~JavaWrapperProtocol() {
  if (close_mode_ == CloseMode::CLOSE_ON_DESTROY) {
    int ret;
    RETRY_ON_EINTR(ret, close(read_fd_));
    RETRY_ON_EINTR(ret, close(write_fd_));
  }
}

template <class M>
Status JavaWrapperProtocol::ReceiveMessage(M* message) {
    switch (serialization_mode_) {
        case SerializationMode::JSON:
        {
            // Read and accumulate one byte at a time, looking for the newline.
            //
            // TODO(adar): it would be more efficient to read a chunk of data, look
            // for a newline, and if found, store the remainder for the next message.
            faststring buf;
            faststring one_byte;
            one_byte.resize(1);
            while (true) {
                RETURN_NOT_OK_PREPEND(DoRead(&one_byte), "unable to receive message byte");
                if (one_byte[0] == '\n') {
                    break;
                }
                buf.push_back(one_byte[0]);
            }

            // Parse the JSON-encoded message.
            const auto& google_status =
                    google::protobuf::util::JsonStringToMessage(buf.ToString(), message);
            if (!google_status.ok()) {
                return Status::InvalidArgument(
                        Substitute("unable to parse JSON: $0", buf.ToString()),
                        google_status.error_message().ToString());
            }
            break;
        }
        case SerializationMode::PB:
        {
            // Read four bytes of size (big-endian).
            faststring size_buf;
            size_buf.resize(sizeof(uint32_t));
            RETURN_NOT_OK_PREPEND(DoRead(&size_buf), "unable to receive message size");
            uint32_t body_size = NetworkByteOrder::Load32(size_buf.data());

            if (body_size > kMaxMessageBytes) {
                return Status::IOError(
                        Substitute("message size ($0) exceeds maximum message size ($1)",
                                   body_size, kMaxMessageBytes));
            }

            // Read the variable size body.
            faststring body_buf;
            body_buf.resize(body_size);
            RETURN_NOT_OK_PREPEND(DoRead(&body_buf), "unable to receive message body");

            // Parse the body into a PB request.
            RETURN_NOT_OK_PREPEND(pb_util::ParseFromArray(
                    message, body_buf.data(), body_buf.length()),
                                  Substitute("unable to parse PB: $0", body_buf.ToString()));
            break;
        }
        default: LOG(FATAL) << "Unknown mode";
    }

    VLOG(1) << "Received message: " << pb_util::SecureDebugString(*message);
    return Status::OK();
}

template <class M>
Status JavaWrapperProtocol::SendMessage(const M& message) {
    VLOG(1) << "Sending message: " << pb_util::SecureDebugString(message);

    faststring buf;
    switch (serialization_mode_) {
        case SerializationMode::JSON:
        {
            string serialized;
            const auto& google_status =
                    google::protobuf::util::MessageToJsonString(message, &serialized);
            if (!google_status.ok()) {
                return Status::InvalidArgument(Substitute(
                        "unable to serialize JSON: $0", pb_util::SecureDebugString(message)),
                                               google_status.error_message().ToString());
            }

            buf.append(serialized);
            buf.append("\n");
            break;
        }
        case SerializationMode::PB:
        {
            size_t msg_size = message.ByteSizeLong();
            buf.resize(sizeof(uint32_t) + msg_size);
            NetworkByteOrder::Store32(buf.data(), msg_size);
            if (!message.SerializeWithCachedSizesToArray(buf.data() + sizeof(uint32_t))) {
                return Status::Corruption("failed to serialize PB to array");
            }
            break;
        }
        default:
            break;
    }
    RETURN_NOT_OK_PREPEND(DoWrite(buf), "unable to send message");
    return Status::OK();
}

Status JavaWrapperProtocol::DoRead(faststring* buf) {
    uint8_t* pos = buf->data();
    size_t rem = buf->length();
    while (rem > 0) {
        ssize_t r;
        RETRY_ON_EINTR(r, read(read_fd_, pos, rem));
        if (r == -1) {
            return Status::IOError("Error reading from pipe", "", errno);
        }
        if (r == 0) {
            return Status::EndOfFile("Other end of pipe was closed");
        }
        DCHECK_GE(rem, r);
        rem -= r;
        pos += r;
    }
    return Status::OK();
}

Status JavaWrapperProtocol::DoWrite(const faststring& buf) {
    const uint8_t* pos = buf.data();
    size_t rem = buf.length();
    while (rem > 0) {
        ssize_t r;
        RETRY_ON_EINTR(r, write(write_fd_, pos, rem));
        if (r == -1) {
            if (errno == EPIPE) {
                return Status::EndOfFile("Other end of pipe was closed");
            }
            return Status::IOError("Error writing to pipe", "", errno);
        }
        DCHECK_GE(rem, r);
        rem -= r;
        pos += r;
    }
    return Status::OK();
}

// Explicit specialization for callers outside this compilation unit.
template
Status JavaWrapperProtocol::ReceiveMessage(EchoResponsePB* message);
template
Status JavaWrapperProtocol::SendMessage(const EchoRequestPB& message);

} // namespace java
} // namespace kudu
