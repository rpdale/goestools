#include "decoder.h"

#include <pthread.h>

#include <cstring>

namespace {

// QueueReader bridges the queue that produces the soft bits
// output of the demodulator to the packetizer.
//
// The packetizer needs an interface that quacks like read(2).
//
class QueueReader : public Reader {
public:
  explicit QueueReader(std::shared_ptr<Queue<std::vector<int8_t> > > queue)
      : queue_(std::move(queue)) {
  }

  virtual ~QueueReader() {
    // Return read buffer if needed
    if (tmp_) {
      queue_->pushRead(std::move(tmp_));
    }
  }

  virtual size_t read(void* buf, size_t count) {
    char* ptr = (char*) buf;
    size_t nread = 0;
    while (nread < count) {
      // Acquire new read buffer if we don't already have one
      if (!tmp_) {
        tmp_ = queue_->popForRead();
        if (!tmp_) {
          // Can't complete read when queue has closed.
          return 0;
        }
        pos_ = 0;
      }

      auto left = tmp_->size() - pos_;
      auto needed = count - nread;
      auto min = std::min(left, needed);
      memcpy(&ptr[nread], &(*tmp_)[pos_], min);

      // Advance cursors
      nread += min;
      pos_ += min;

      // Return read buffer if it was exhausted
      if (pos_ == tmp_->size()) {
        queue_->pushRead(std::move(tmp_));
      }
    }

    return nread;
  }

protected:
  std::shared_ptr<Queue<std::vector<int8_t> > > queue_;
  std::unique_ptr<std::vector<int8_t> > tmp_;
  size_t pos_;
};

} // namespace

Decoder::Decoder(std::shared_ptr<Queue<std::vector<int8_t> > > queue) {
  packetizer_ = std::make_unique<Packetizer>(
    std::make_shared<QueueReader>(std::move(queue)));
  packetPublisher_ = Publisher::create("tcp://0.0.0.0:5004");
}

void Decoder::start() {
  thread_ = std::thread([&] {
      std::array<uint8_t, 892> buf;
      while (packetizer_->nextPacket(buf, nullptr)) {
        packetPublisher_->publish(buf);
      }
    });
  pthread_setname_np(thread_.native_handle(), "decoder");
}

void Decoder::stop() {
  thread_.join();
}