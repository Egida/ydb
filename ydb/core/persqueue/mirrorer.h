#pragma once

#include "actor_persqueue_client_iface.h"

#include <library/cpp/actors/core/hfunc.h>
#include <library/cpp/actors/core/log.h>
#include <ydb/core/base/appdata.h>
#include <ydb/core/persqueue/percentile_counter.h>
#include <ydb/core/protos/counters_pq.pb.h>
#include <ydb/core/protos/pqconfig.pb.h>
#include <ydb/public/lib/base/msgbus.h>
#include <ydb/core/persqueue/events/internal.h>
#include <ydb/library/persqueue/counter_time_keeper/counter_time_keeper.h>
#include <ydb/public/sdk/cpp/client/ydb_persqueue_core/persqueue.h>


namespace NKikimr {
namespace NPQ {

class TMirrorer : public TActorBootstrapped<TMirrorer> {
private:
    const ui64 MAX_BYTES_IN_FLIGHT = 8 * 1024 * 1024;
    const TDuration WRITE_RETRY_TIMEOUT_MAX = TDuration::Seconds(1);
    const TDuration WRITE_RETRY_TIMEOUT_START = TDuration::MilliSeconds(1);

    const TDuration CONSUMER_INIT_TIMEOUT_MAX = TDuration::Seconds(60);
    const TDuration CONSUMER_INIT_TIMEOUT_START = TDuration::Seconds(5);

    const TDuration CONSUMER_INIT_INTERVAL_MAX = TDuration::Seconds(60);
    const TDuration CONSUMER_INIT_INTERVAL_START = TDuration::Seconds(1);

    const TDuration READ_RETRY_TIMEOUT_MAX = TDuration::Seconds(1);
    const TDuration READ_RETRY_TIMEOUT_START = TDuration::MilliSeconds(1);

    const TDuration UPDATE_COUNTERS_INTERVAL = TDuration::Seconds(5);

    const TDuration LOG_STATE_INTERVAL = TDuration::Minutes(1);
    const TDuration INIT_TIMEOUT = TDuration::Minutes(1);
    const TDuration WRITE_TIMEOUT = TDuration::Minutes(10);


private:
    enum EEventCookie : ui64 {
        WRITE_REQUEST_COOKIE = 1,
        UPDATE_WRITE_TIMESTAMP = 2
    };

    struct TMessageInfo {
        ui64 Offset;
        ui64 Size;
        TInstant WriteTime;

        TMessageInfo(ui64 offset, ui64 size, TInstant writeTime)
            : Offset(offset)
            , Size(size)
            , WriteTime(writeTime)
        {}
    };

private:

    STFUNC(StateInitConsumer)
    {
        NPersQueue::TCounterTimeKeeper keeper(Counters.Cumulative()[COUNTER_PQ_TABLET_CPU_USAGE]);

        TRACE_EVENT(NKikimrServices::PQ_MIRRORER);
        switch (ev->GetTypeRewrite()) {
            HFuncTraced(TEvPQ::TEvInitCredentials, HandleInitCredentials);
            HFuncTraced(TEvPQ::TEvChangeConfig, HandleChangeConfig);
            HFuncTraced(TEvPQ::TEvCreateConsumer, CreateConsumer);
            HFuncTraced(TEvPQ::TEvRetryWrite, HandleRetryWrite);
            HFuncTraced(TEvPersQueue::TEvResponse, Handle);
            HFuncTraced(TEvPQ::TEvUpdateCounters, Handle);
            HFuncTraced(TEvents::TEvPoisonPill, Handle);
        default:
            break;
        };
    }

    STFUNC(StateWork)
    {
        NPersQueue::TCounterTimeKeeper keeper(Counters.Cumulative()[COUNTER_PQ_TABLET_CPU_USAGE]);

        TRACE_EVENT(NKikimrServices::PQ_MIRRORER);
        switch (ev->GetTypeRewrite()) {
            HFuncTraced(TEvPQ::TEvChangeConfig, HandleChangeConfig);
            CFunc(TEvents::TSystem::Wakeup, HandleWakeup);
            HFuncTraced(TEvPQ::TEvRequestPartitionStatus, RequestSourcePartitionStatus);
            HFuncTraced(TEvPQ::TEvRetryWrite, HandleRetryWrite);
            HFuncTraced(TEvPersQueue::TEvResponse, Handle);
            HFuncTraced(TEvPQ::TEvUpdateCounters, Handle);
            HFuncTraced(TEvPQ::TEvReaderEventArrived, ProcessNextReaderEvent);
            HFuncTraced(TEvents::TEvPoisonPill, Handle);
        default:
            break;
        };
    }

private:
    template<class TEvent>
    void ScheduleWithIncreasingTimeout(const TActorId& recipient, TDuration& timeout, const TDuration& maxTimeout, const TActorContext &ctx) { 
        ctx.ExecutorThread.ActorSystem->Schedule(timeout, new IEventHandle(recipient, SelfId(), new TEvent()));
        timeout = Min(timeout * 2, maxTimeout);
    }

    bool AddToWriteRequest(
        NKikimrClient::TPersQueuePartitionRequest& request,
        NYdb::NPersQueue::TReadSessionEvent::TDataReceivedEvent::TCompressedMessage& message
    );
    void ProcessError(const TActorContext& ctx, const TString& msg);
    void ProcessError(const TActorContext& ctx, const TString& msg, const NKikimrClient::TResponse& response);
    void AfterSuccesWrite(const TActorContext& ctx);
    void ProcessWriteResponse(
        const TActorContext& ctx,
        const NKikimrClient::TPersQueuePartitionResponse& response
    );
    void ScheduleConsumerCreation(const TActorContext& ctx);
    void RecreateCredentialsProvider(const TActorContext& ctx);
    void StartInit(const TActorContext& ctx);
    void RetryWrite(const TActorContext& ctx);

    void ProcessNextReaderEvent(TEvPQ::TEvReaderEventArrived::TPtr& ev, const TActorContext& ctx);

    TString MirrorerDescription() const;

    TString GetCurrentState() const;

public:
    static constexpr NKikimrServices::TActivity::EType ActorActivityType();
    TMirrorer(
        TActorId tabletActor,
        TActorId partitionActor,
        const TString& topicName,
        ui32 partition,
        bool localDC,
        ui64 endOffset,
        const NKikimrPQ::TMirrorPartitionConfig& config,
        const TTabletCountersBase& counters
    );
    void Bootstrap(const TActorContext& ctx);
    void Handle(TEvents::TEvPoisonPill::TPtr& ev, const TActorContext& ctx);
    void Handle(TEvPersQueue::TEvResponse::TPtr& ev, const TActorContext& ctx);
    void Handle(TEvPQ::TEvUpdateCounters::TPtr& ev, const TActorContext& ctx);
    void HandleChangeConfig(TEvPQ::TEvChangeConfig::TPtr& ev, const TActorContext& ctx);
    void TryToRead(const TActorContext& ctx);
    void TryToWrite(const TActorContext& ctx);
    void HandleInitCredentials(TEvPQ::TEvInitCredentials::TPtr& ev, const TActorContext& ctx);
    void HandleRetryWrite(TEvPQ::TEvRetryWrite::TPtr& ev, const TActorContext& ctx);
    void HandleWakeup(const TActorContext& ctx);
    void CreateConsumer(TEvPQ::TEvCreateConsumer::TPtr& ev, const TActorContext& ctx);
    void RequestSourcePartitionStatus(TEvPQ::TEvRequestPartitionStatus::TPtr& ev, const TActorContext& ctx);
    void RequestSourcePartitionStatus();
    void TryUpdateWriteTimetsamp(const TActorContext &ctx);
    void AddMessagesToQueue(
        TVector<NYdb::NPersQueue::TReadSessionEvent::TDataReceivedEvent::TCompressedMessage>&& messages
    );
    void StartWaitNextReaderEvent(const TActorContext& ctx);

private:
    TActorId TabletActor;
    TActorId PartitionActor; 
    TString TopicName;
    ui32 Partition;
    bool LocalDC;
    ui64 EndOffset;
    ui64 OffsetToRead;
    NKikimrPQ::TMirrorPartitionConfig Config;

    TDeque<NYdb::NPersQueue::TReadSessionEvent::TDataReceivedEvent::TCompressedMessage> Queue;
    TDeque<TMessageInfo> WriteInFlight;
    ui64 BytesInFlight = 0;
    std::optional<NKikimrClient::TPersQueuePartitionRequest> WriteRequestInFlight;
    TDuration WriteRetryTimeout = WRITE_RETRY_TIMEOUT_START;
    TInstant WriteRequestTimestamp;
    std::shared_ptr<NYdb::ICredentialsProviderFactory> CredentialsProvider;
    std::shared_ptr<NYdb::NPersQueue::IReadSession> ReadSession;
    NYdb::NPersQueue::TPartitionStream::TPtr PartitionStream;
    THolder<NYdb::NPersQueue::TReadSessionEvent::TPartitionStreamStatusEvent> StreamStatus;
    TInstant LastInitStageTimestamp;

    TDuration ConsumerInitTimeout = CONSUMER_INIT_TIMEOUT_START;
    TDuration ConsumerInitInterval = CONSUMER_INIT_INTERVAL_START;
    TDuration ReadRetryTimeout = READ_RETRY_TIMEOUT_START;

    TTabletCountersBase Counters;

    bool WaitNextReaderEventInFlight = false;

    bool WasSuccessfulRecording = false;
    TInstant LastStateLogTimestamp;

    TMultiCounter MirrorerErrors;
    TMultiCounter InitTimeoutCounter;
    TMultiCounter WriteTimeoutCounter;
    THolder<TPercentileCounter> MirrorerTimeLags;
};

}// NPQ
}// NKikimr
