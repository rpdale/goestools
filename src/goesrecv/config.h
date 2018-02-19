#pragma once

#include <memory>
#include <string>

#include "packet_publisher.h"
#include "sample_publisher.h"
#include "soft_bit_publisher.h"
#include "stats_publisher.h"

struct Config {
  struct Demodulator {
    // LRIT or HRIT
    std::string downlinkType;

    // String "airspy" or "rtlsdr"
    std::string source;

    // Demodulator statistics (gain, frequency correction, etc.)
    std::unique_ptr<StatsPublisher> statsPublisher;
  };

  Demodulator demodulator;

  struct Nanomsg {
    // Address to connect to
    std::string connect;

    // Optional receive buffer size
    size_t receiveBuffer = 0;

    // Required sample rate of stream
    uint32_t sampleRate = 0;
  };

  Nanomsg nanomsg;

  struct Source {
    std::unique_ptr<SamplePublisher> samplePublisher;
  };

  Source source;

  struct AGC {
    std::unique_ptr<SamplePublisher> samplePublisher;
  };

  AGC agc;

  struct Costas {
    std::unique_ptr<SamplePublisher> samplePublisher;
  };

  Costas costas;

  struct RRC {
    std::unique_ptr<SamplePublisher> samplePublisher;
  };

  RRC rrc;

  struct ClockRecovery {
    std::unique_ptr<SamplePublisher> samplePublisher;
  };

  ClockRecovery clockRecovery;

  struct Quantization {
    std::unique_ptr<SoftBitPublisher> softBitPublisher;
  };

  Quantization quantization;

  struct Decoder {
    std::unique_ptr<PacketPublisher> packetPublisher;

    // Decoder statistics (Viterbi, Reed-Solomon, etc.)
    std::unique_ptr<StatsPublisher> statsPublisher;
  };

  Decoder decoder;

  static Config load(const std::string& file);
};