package org.apache.kudu.ranger;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.BufferedInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.BlockingQueue;

public class MessageReader implements Runnable {
  public static final Logger LOG = LoggerFactory.getLogger(MessageReader.class);
  private BlockingQueue<String> blockingQueue;
  private boolean done = false;

  public MessageReader(BlockingQueue<String> blockingQueue) {
    this.blockingQueue = blockingQueue;
  }

  @Override
  public void run() {
    try {
      BufferedInputStream bis = new BufferedInputStream(System.in);
      while (bis.available() > 0) {
        String data = parseBinaryInput(bis);
        // put on the queue
        try {
          blockingQueue.put(data);
        } catch (InterruptedException e) {
          LOG.error(e.toString());
        }
      }
      bis.close();
      done = true;
    } catch (IOException ex) {
      LOG.error(ex.toString());
    }
  }

  private String parseBinaryInput(BufferedInputStream inputStream) throws IOException {
    byte[] size = new byte[4];
    inputStream.read(size, 0, 4);
    int len = fromArray(size);
    byte[] data = new byte[len];
    inputStream.read(data, 0, len);
    String ret = new String(data, "UTF-8");
    return ret;
  }

  private String pasreJsonInput(BufferedInputStream inputStream) throws IOException {
    StringBuilder sb = new StringBuilder();
    char c = (char)inputStream.read();
    while (!String.valueOf(c).matches("\n")) {
      sb.append(c);
      c = (char)inputStream.read();
    }
    return sb.toString();
  }

  private int fromArray(byte[] data) {
    ByteBuffer buffer = ByteBuffer.wrap(data);
    buffer.order(ByteOrder.BIG_ENDIAN);
    return buffer.getInt();
  }

  public boolean isDone() {
    return done;
  }
}
