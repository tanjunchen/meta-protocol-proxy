#pragma once

#include "envoy/common/time.h"
#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/timespan.h"

#include "source/common/common/logger.h"

#include "api/meta_protocol_proxy/v1alpha/meta_protocol_proxy.pb.h"

#include "src/meta_protocol_proxy/codec/codec.h"
#include "src/meta_protocol_proxy/active_message.h"
#include "src/meta_protocol_proxy/decoder.h"
#include "src/meta_protocol_proxy/decoder_event_handler.h"
#include "src/meta_protocol_proxy/filters/filter.h"
#include "src/meta_protocol_proxy/stats.h"
#include "src/meta_protocol_proxy/route/rds.h"
#include "src/meta_protocol_proxy/stream.h"
#include "src/meta_protocol_proxy/tracing/tracer.h"
#include "src/meta_protocol_proxy/request_id/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

/**
 * Config is a configuration interface for ConnectionManager.
 */
class Config {
public:
  virtual ~Config() = default;

  virtual FilterChainFactory& filterFactory() PURE;
  virtual MetaProtocolProxyStats& stats() PURE;
  virtual CodecPtr createCodec() PURE;
  virtual Route::Config& routerConfig() PURE;
  virtual std::string applicationProtocol() PURE;
  virtual absl::optional<std::chrono::milliseconds> idleTimeout() PURE;
  /**
   * @return Route::RouteConfigProvider* the configuration provider used to acquire a route
   *         config for each request flow. Pointer ownership is _not_ transferred to the caller of
   *         this function.
   */
  virtual Route::RouteConfigProvider* routeConfigProvider() PURE;
  virtual Tracing::MetaProtocolTracerSharedPtr tracer() PURE;
  virtual Tracing::TracingConfig* tracingConfig() PURE;
  virtual RequestIDExtensionSharedPtr requestIDExtension() PURE;
  virtual const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const PURE;
};

// class ActiveMessagePtr;
class ConnectionManager : public Network::ReadFilter,
                          public Network::ConnectionCallbacks,
                          public RequestDecoderCallbacks,
                          Logger::Loggable<Logger::Id::filter> {
public:
  ConnectionManager(Config& config, Random::RandomGenerator& random_generator,
                    TimeSource& time_system);
  ~ConnectionManager() override {
    ENVOY_LOG(trace, "********** ConnectionManager destructed ***********");
  };

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

  // RequestDecoderCallbacks
  MessageHandler& newMessageHandler() override;
  bool onHeartbeat(MetadataSharedPtr metadata) override;

  MetaProtocolProxyStats& stats() const { return stats_; }
  Network::Connection& connection() const { return read_callbacks_->connection(); }
  TimeSource& timeSystem() const { return time_system_; }
  Random::RandomGenerator& randomGenerator() const { return random_generator_; }
  Config& config() const { return config_; }

  void deferredDeleteMessage(ActiveMessage& message);
  void sendLocalReply(Metadata& metadata, const DirectResponse& response, bool end_stream);

  Stream& newActiveStream(uint64_t stream_id);
  Stream& getActiveStream(uint64_t stream_id);
  bool streamExisted(uint64_t stream_id);
  void closeStream(uint64_t stream_id);
  void clearStream() { active_stream_map_.clear(); }

  Tracing::MetaProtocolTracerSharedPtr tracer() { return config_.tracer(); };
  Tracing::TracingConfig* tracingConfig() { return config_.tracingConfig(); };
  RequestIDExtensionSharedPtr requestIDExtension() { return config_.requestIDExtension(); };
  const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() { return config_.accessLogs(); };

  // This function is for testing only.
  std::list<ActiveMessagePtr>& getActiveMessagesForTest() { return active_message_list_; }

private:
  void dispatch();
  void resetAllMessages(bool local_reset);

  // This function is to deal with idle downstream's connection timeout.
  void onIdleTimeout();
  // Reset the timer.
  void resetIdleTimer();
  // Disable the timer
  void disableIdleTimer();

  Buffer::OwnedImpl request_buffer_;
  std::list<ActiveMessagePtr> active_message_list_;
  std::map<uint64_t, StreamPtr> active_stream_map_;

  Config& config_;
  TimeSource& time_system_;
  MetaProtocolProxyStats& stats_;
  Random::RandomGenerator& random_generator_;

  CodecPtr codec_;
  RequestDecoderPtr decoder_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  // timer for idle timeout
  Event::TimerPtr idle_timer_;
};

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
