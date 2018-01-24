#include "rtlsdr_source.h"

#include <pthread.h>

#include <cassert>
#include <climits>
#include <cmath>
#include <iostream>

#include <arm_neon.h>

std::unique_ptr<RTLSDR> RTLSDR::open(uint32_t index) {
  rtlsdr_dev_t* dev = nullptr;
  auto rv = rtlsdr_open(&dev, index);
  if (rv < 0) {
    std::cerr << "Unable to open RTLSDR device..." << std::endl;
    exit(1);
  }

  return std::make_unique<RTLSDR>(dev);
}

RTLSDR::RTLSDR(rtlsdr_dev_t* dev) : dev_(dev) {
  int rv;

  // First get the number of gain settings
  rv = rtlsdr_get_tuner_gains(dev_, nullptr);
  assert(rv > 0);

  // Now fill our vector with valid gain settings
  tunerGains_.resize(rv);
  rv = rtlsdr_get_tuner_gains(dev_, tunerGains_.data());
  assert(rv >= 0);
}

RTLSDR::~RTLSDR() {
  if (dev_ != nullptr) {
    rtlsdr_close(dev_);
  }
}

void RTLSDR::setCenterFrequency(uint32_t freq) {
  assert(dev_ != nullptr);
  auto rv = rtlsdr_set_center_freq(dev_, freq);
  assert(rv >= 0);
}

void RTLSDR::setSampleRate(uint32_t rate) {
  assert(dev_ != nullptr);
  auto rv = rtlsdr_set_sample_rate(dev_, rate);
  assert(rv >= 0);
}

void RTLSDR::setTunerGain(int db) {
  int rv;

  // Find closest valid gain setting
  float resultDist = INT_MAX;
  int resultGain = 0;
  for (const auto& gain : tunerGains_) {
    float dist = fabsf((gain / 10.0f) - db);
    if (dist < resultDist) {
      resultDist = dist;
      resultGain = gain;
    }
  }

  rv = rtlsdr_set_tuner_gain(dev_, resultGain);
  assert(rv >= 0);
}

void RTLSDR::setPublisher(std::unique_ptr<Publisher> publisher) {
  publisher_ = std::move(publisher);
}

static void rtlsdr_callback(unsigned char* buf, uint32_t len, void* ptr) {
  RTLSDR* rtlsdr = reinterpret_cast<RTLSDR*>(ptr);
  rtlsdr->handle(buf, len);
}

void RTLSDR::start(const std::shared_ptr<Queue<Samples> >& queue) {
  assert(dev_ != nullptr);
  rtlsdr_reset_buffer(dev_);
  queue_ = queue;
  thread_ = std::thread([&] {
      rtlsdr_read_async(dev_, rtlsdr_callback, this, 0, 0);
    });
  pthread_setname_np(thread_.native_handle(), "rtlsdr");
}

void RTLSDR::stop() {
  assert(dev_ != nullptr);
  auto rv = rtlsdr_cancel_async(dev_);
  assert(rv >= 0);

  // Wait for thread to terminate
  thread_.join();

  // Close queue to signal downstream
  queue_->close();

  // Clear reference to queue
  queue_.reset();
}

void RTLSDR::handle(unsigned char* buf, uint32_t len) {
  uint32_t nsamples = len / 2;

  // Expect multiple of 4
  assert((nsamples & 0x3) == 0);

  // Grab buffer from queue
  auto out = queue_->popForWrite();
  out->resize(nsamples);

  // // Number to subtract from samples for normalization
  // // See http://cgit.osmocom.org/gr-osmosdr/tree/lib/rtl/rtl_source_c.cc#n176
  // simdpp::float32<4> norm = simdpp::splat(127.4f / 128.0f);
  //
  // // Iterate over samples in blocks of 4 (each sample has I and Q)
  // for (uint32_t i = 0; i < (nsamples / 4); i++) {
  //   simdpp::uint32<4> ui;
  //   simdpp::uint32<4> uq;
  //   simdpp::load_packed2(ui, uq, &buf[i * 8]);
  //
  //   // Convert to float32 and divide by 128
  //   simdpp::float32<4> fi = simdpp::to_float32(ui) * (1.0f / 128.0f);
  //   simdpp::float32<4> fq = simdpp::to_float32(ui) * (1.0f / 128.0f);
  //
  //   // Subtract to normalize to [-1.0, 1.0]
  //   fi = fi - norm;
  //   fq = fq - norm;
  //
  //   // Store in output
  //   simdpp::store_packed2(&(out->data()[i * 4]), fi, fq);
  // }

  // Number to subtract from samples for normalization
  // See http://cgit.osmocom.org/gr-osmosdr/tree/lib/rtl/rtl_source_c.cc#n176
  const float32_t norm_ = 127.4f / 128.0f;
  float32x4_t norm = vld1q_dup_f32(&norm_);

  // Iterate over samples in blocks of 4 (each sample has I and Q)
  for (uint32_t i = 0; i < (nsamples / 4); i++) {
    uint32x4_t ui;
    ui[0] = buf[i * 8 + 0];
    ui[1] = buf[i * 8 + 2];
    ui[2] = buf[i * 8 + 4];
    ui[3] = buf[i * 8 + 6];

    uint32x4_t uq;
    uq[0] = buf[i * 8 + 1];
    uq[1] = buf[i * 8 + 3];
    uq[2] = buf[i * 8 + 5];
    uq[3] = buf[i * 8 + 7];

    // Convert to float32x4 and divide by 2^7 (128)
    float32x4_t fi = vcvtq_n_f32_u32(ui, 7);
    float32x4_t fq = vcvtq_n_f32_u32(uq, 7);

    // Subtract to normalize to [-1.0, 1.0]
    float32x4x2_t fiq;
    fiq.val[0] = vsubq_f32(fi, norm);
    fiq.val[1] = vsubq_f32(fq, norm);

    // Store in output
    vst2q_f32((float*) (&out->data()[i * 4]), fiq);
  }

  // Publish output if applicable
  if (publisher_) {
    publisher_->publish(*out);
  }

  // Return buffer to queue
  queue_->pushWrite(std::move(out));
}