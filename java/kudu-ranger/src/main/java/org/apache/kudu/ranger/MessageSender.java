package org.apache.kudu.ranger;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protobuf.util.JsonFormat;
import org.apache.kudu.java.Java;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.concurrent.BlockingQueue;

public class MessageSender implements Runnable {
  private BlockingQueue<String> blockingQueue;
    public static final Logger LOG = LoggerFactory.getLogger(MessageSender.class);
  private final String DONE = new String();

  public MessageSender(BlockingQueue<String> blockingQueue) {
    this.blockingQueue = blockingQueue;
  }

  @Override
  public void run() {
     while (true) {
       try {
         String data = blockingQueue.take();
         if (data == DONE) {
           //notify other threads to stop
           blockingQueue.add(DONE);
           return;
         }

         sendBinaryMessage(data);
       } catch (InterruptedException | IOException e) {
         LOG.error(e.toString());
       }
     }
  }

  private void sendJsonMessage(String data) throws InvalidProtocolBufferException {
    // Parse the message
    Java.EchoRequestPB.Builder pb = Java.EchoRequestPB.newBuilder();
    JsonFormat.parser().merge(data, pb);
    Java.EchoRequestPB request = pb.build();

    // Serialize the response to JSON
    Java.EchoResponsePB.Builder resBuilder = Java.EchoResponsePB.newBuilder();
    resBuilder.setData(request.getData());
    Java.EchoResponsePB res = resBuilder.build();
    String output = JsonFormat.printer().print(res).replaceAll("\n", "");
    output = output.replaceAll(" ", "");
    System.out.print(output + "\n");
  }

  private void sendBinaryMessage(String data) throws IOException {
    // Parse the message
    Java.EchoRequestPB request = Java.EchoRequestPB.parseFrom(data.getBytes());

    // Serialize the response
    Java.EchoResponsePB.Builder resBuilder = Java.EchoResponsePB.newBuilder();
    resBuilder.setData(request.getData());
    Java.EchoResponsePB res = resBuilder.build();
    byte[] size = toArray(res.getSerializedSize());
    byte[] payload = res.toByteArray();
    byte[] msg = new byte[4 + res.getSerializedSize()];
    System.arraycopy(size, 0, msg, 0, size.length);
    System.arraycopy(payload, 0, msg, size.length, payload.length);
    System.out.write(msg);
  }

  private byte[] toArray(int value) {
    return ByteBuffer.allocate(4).order(ByteOrder.BIG_ENDIAN).putInt(value).array();
  }

  // this is used to signal from the main thread that the producer
  // has finished adding stuff to the queue
  public void finish() {
    blockingQueue.add(DONE);
  }
}
