#pragma once
#include "defs.h"
#include <util/generic/singleton.h>
#include <util/generic/vector.h>
#include <library/cpp/deprecated/enum_codegen/enum_codegen.h>
#include <library/cpp/monlib/service/pages/templates.h> 
#include <google/protobuf/descriptor.pb.h>
#include <ydb/core/protos/counters.pb.h>
#include <ydb/core/protos/tablet_counters.pb.h>

////////////////////////////////////////////
namespace NKikimr {

#define COUNTER_TEXT_ARRAY(name, value, ...) value,

#define COUNTER_PERCENTILE_CONFIG_ARRAY(rangeVal, rangeText, ...) { rangeVal, rangeText },

template <typename T>
class TCountersArray;

////////////////////////////////////////////
/// The TTabletSimpleCounterBase class
////////////////////////////////////////////
class TTabletSimpleCounterBase {
public:
    //
    TTabletSimpleCounterBase()
        : Value(0)
    {}
    ~TTabletSimpleCounterBase() {}

    //
    ui64 Get() const {
        return Value;
    }

    void OutputHtml(IOutputStream &os, const char* name) const;

protected:
    //
    ui64 Value;
};

////////////////////////////////////////////
/// The TTabletSimpleCounter class
////////////////////////////////////////////
class TTabletSimpleCounter : public TTabletSimpleCounterBase {
    friend class TCountersArray<TTabletSimpleCounter>;
public:
    //
    TTabletSimpleCounter()
    {}
    ~TTabletSimpleCounter()
    {}

    //
    TTabletSimpleCounter& Set(ui64 value) {
        Value = value;
        return *this;
    }

    TTabletSimpleCounter& Add(ui64 delta) {
        Value += delta;
        return *this;
    }

    TTabletSimpleCounter& Sub(ui64 delta) {
        Value -= delta;
        return *this;
    }

    TTabletSimpleCounter& operator = (ui64 value) {
        return Set(value);
    }

    TTabletSimpleCounter& operator += (ui64 delta) {
        return Add(delta);
    }

    TTabletSimpleCounter& operator -= (ui64 delta) {
        return Sub(delta);
    }

private:
    //
    void Initialize(const TTabletSimpleCounter& rp) {
        SetTo(rp);
    }
    void AdjustToBaseLine(const TTabletSimpleCounter&) { /* no-op */ }
    void SetTo(const TTabletSimpleCounter& rp) {
        Value = rp.Value;
    }

    void Populate(const TTabletSimpleCounter& rp) {
        SetTo(rp);
    }
};

////////////////////////////////////////////
/// The TTabletCumulativeCounter class
////////////////////////////////////////////
class TTabletCumulativeCounter : public TTabletSimpleCounterBase{
    friend class TCountersArray<TTabletCumulativeCounter>;
public:
    //
    TTabletCumulativeCounter()
    {}
    ~TTabletCumulativeCounter()
    {}

    //
    TTabletCumulativeCounter& Increment(ui64 delta) {
        Value += delta;
        return *this;
    }
    TTabletCumulativeCounter& operator += (ui64 delta) {
        return Increment(delta);
    }

private:
    //
    void Initialize(const TTabletCumulativeCounter& rp) {
        SetTo(rp);
    }
    void AdjustToBaseLine(const TTabletCumulativeCounter& baseLine) {
        Y_VERIFY_DEBUG(Value >= baseLine.Value);
        Value -= baseLine.Value;
    }
    void SetTo(const TTabletCumulativeCounter& rp) {
        Value = rp.Value;
    }

    void Populate(const TTabletCumulativeCounter& rp) {
        Value += rp.Value;
    }
};

////////////////////////////////////////////
/// The TTabletPercentileCounter class
////////////////////////////////////////////
class TTabletPercentileCounter : TNonCopyable {
    friend class TCountersArray<TTabletPercentileCounter>;
public:
    //
    struct TRangeDef {
        ui64 RangeVal;
        const char* RangeName;

        friend bool operator <(const TRangeDef& x, const TRangeDef& y) { return x.RangeVal < y.RangeVal; }
        friend bool operator <(const ui64 x, const TRangeDef& y) { return x < y.RangeVal; }
    };

    template <ui32 rangeCount>
    void Initialize(const TRangeDef(&ranges)[rangeCount], bool integral) {
        Initialize(rangeCount, ranges, integral);
    }

    TTabletPercentileCounter& IncrementFor(ui64 what) {
        ui32 index = FindSlot(what);
        Values[index] += 1;
        return *this;
    }

    TTabletPercentileCounter& IncrementForRange(ui64 idx) {
        Values[idx] += 1;
        return *this;
    }

    TTabletPercentileCounter& DecrementFor(ui64 what) {
        Y_VERIFY(Integral);
        ui32 index = FindSlot(what);
        Y_VERIFY_DEBUG(Values[index] > 0);
        Values[index] -= 1;
        return *this;
    }

    ui32 GetRangeCount() const {
        return RangeCount;
    }

    const char* GetRangeName(ui32 index) const {
        return Ranges[index].RangeName;
    }

    ui64 GetRangeValue(ui32 index) const {
        return Values[index];
    }

    ui64 GetRangeBound(ui32 index) const {
        return Ranges[index].RangeVal;
    }

    TVector<TRangeDef> GetRanges() const {
        return TVector<TRangeDef>(Ranges, Ranges + RangeCount);
    }

    bool GetIntegral() const {
        return Integral;
    }

    void PopulateFrom(const TTabletPercentileCounter& rp) {
        Populate(rp);
    }

    void OutputHtml(IOutputStream &os, const char* name) const;

private:
    //
    void AdjustToBaseLine(const TTabletPercentileCounter& baseLine) {
        //
        Y_VERIFY_DEBUG(RangeCount == baseLine.RangeCount);
        if (Integral) {
            return;
        }

        for (ui32 i = 0; i < RangeCount; ++i) {
            Y_VERIFY_DEBUG(Values[i] >= baseLine.Values[i]);
            Values[i] -= baseLine.Values[i];
        }
    }

    void Initialize(const TTabletPercentileCounter& rp) {
        //
        if (rp.IsInitialized()) {
            Initialize(rp.RangeCount, rp.Ranges, rp.Integral);
            SetTo(rp);
        }
    }

    //
    void SetTo(const TTabletPercentileCounter& rp) {
        //
        Y_VERIFY_DEBUG(RangeCount == rp.RangeCount);
        for (ui32 i = 0; i < RangeCount; ++i) {
            Values[i] = rp.Values[i];
        }
    }

public:
    void Initialize(ui32 rangeCount, const TRangeDef* ranges, bool integral) {
        Y_VERIFY_DEBUG(!Ranges);
        Y_VERIFY_DEBUG(!Values);
        Y_VERIFY_DEBUG(rangeCount > 0);
        Y_VERIFY_DEBUG(ranges[0].RangeVal == 0);

        RangeCount = rangeCount;
        Ranges = ranges;
        Integral = integral;

        Y_VERIFY_DEBUG(IsSorted());

        Values = TArrayHolder<ui64>(new ui64[RangeCount]());
    }

    void Clear() {
        if (IsInitialized()) {
            std::fill(&Values[0], &Values[RangeCount], 0);
        }
    }

private:
    //
    ui32 FindSlot(ui64 what) const {
        return Max<ssize_t>(0, std::upper_bound(Ranges, Ranges + RangeCount, what) - Ranges - 1);
    }

    bool IsSorted() const {
        return std::is_sorted(Ranges, Ranges + RangeCount);
    }

    bool IsInitialized() const {
        return RangeCount != 0;
    }

    void Populate(const TTabletPercentileCounter& rp) {
        if (IsInitialized()) {
            Y_VERIFY_DEBUG(RangeCount == rp.RangeCount);
            for (ui32 i = 0; i < RangeCount; ++i) {
                Values[i] += rp.Values[i];
            }
        } else {
            Initialize(rp);
        }
    }

    //
    ui32 RangeCount = 0;
    const TRangeDef* Ranges = nullptr;
    bool Integral = false;

    TArrayHolder<ui64> Values;
};

////////////////////////////////////////////
template <typename T>
class TCountersArray : TNonCopyable {
    friend class TTabletCountersBase;
    friend class TTabletLabeledCountersBase;
public:
    typedef std::shared_ptr<T> TCountersHolder;
    //
    TCountersArray(ui32 countersQnt)
        : CountersQnt(countersQnt)
        , CountersHolder(nullptr)
        , Counters(nullptr)
    {
        if (CountersQnt) {
            CountersHolder.reset(new T[CountersQnt](), &CheckedArrayDelete<T>);
            Counters = CountersHolder.get();
        }
    }

    //not owning constructor - can refer to part of other counters
    TCountersArray(TCountersHolder& countersHolder, T* counters, const ui32 countersQnt)
        : CountersQnt(countersQnt)
        , CountersHolder(countersHolder)
        , Counters(counters)
    {
    }

    ~TCountersArray()
    {
        Counters = nullptr;
        CountersQnt = 0;
    }

    //
    explicit operator bool() const {
        return Counters;
    }

    //
    T& operator[] (ui32 index) {
        Y_ASSERT(index < CountersQnt);
        return Counters[index];
    }

    const T& operator[] (ui32 index) const {
        Y_ASSERT(index < CountersQnt);
        return Counters[index];
    }

    ui32 Size() const {
        return CountersQnt;
    }

private:
    //
    void Reset(const TCountersArray<T>& rp) {
        Y_VERIFY(!CountersQnt);
        CountersHolder.reset();
        Counters = nullptr;

        CountersQnt = rp.CountersQnt;
        if (CountersQnt) {
            CountersHolder.reset(new T[CountersQnt](), &CheckedArrayDelete<T>);
            Counters = CountersHolder.get();
        }

        for (ui32 i = 0, e = CountersQnt; i < e; ++i) {
            Counters[i].Initialize(rp.Counters[i]);
        }
    }

    //
    void AdjustToBaseLine(const TCountersArray<T>& baseLine) {
        Y_VERIFY_DEBUG(baseLine.CountersQnt == CountersQnt);
        for (ui32 i = 0, e = CountersQnt; i < e; ++i) {
            Counters[i].AdjustToBaseLine(baseLine.Counters[i]);
        }
    }

    void SetTo(const TCountersArray<T>& rp) {
        Y_VERIFY_DEBUG(rp.CountersQnt == CountersQnt);
        for (ui32 i = 0, e = CountersQnt; i < e; ++i) {
            Counters[i].SetTo(rp.Counters[i]);
        }
    }

    void Populate(const TCountersArray<T>& rp) {
        if (CountersQnt != rp.CountersQnt) {
            Reset(rp);
        } else {
            for (ui32 i = 0, e = CountersQnt; i < e; ++i) {
                Counters[i].Populate(rp.Counters[i]);
            }
        }
    }

    //
    ui32 CountersQnt;
    TCountersHolder CountersHolder;
    T* Counters;
};

////////////////////////////////////////////
/// The TTabletCountersBase class
////////////////////////////////////////////
class TTabletCountersBase {
public:
    //
    TTabletCountersBase()
        : SimpleCounters(0)
        , CumulativeCounters(0)
        , PercentileCounters(0)
        , SimpleCountersMetaInfo(nullptr)
        , CumulativeCountersMetaInfo(nullptr)
        , PercentileCountersMetaInfo(nullptr)
    {}

    TTabletCountersBase(ui32 simpleCountersQnt,
        ui32 cumulativeCountersQnt,
        ui32 percentileCountersQnt,
        const char* const * simpleCountersMetaInfo,
        const char* const * cumulativeCountersMetaInfo,
        const char* const * percentileCountersMetaInfo)
        : SimpleCounters(simpleCountersQnt)
        , CumulativeCounters(cumulativeCountersQnt)
        , PercentileCounters(percentileCountersQnt)
        , SimpleCountersMetaInfo(simpleCountersMetaInfo)
        , CumulativeCountersMetaInfo(cumulativeCountersMetaInfo)
        , PercentileCountersMetaInfo(percentileCountersMetaInfo)
    {}

    //Constructor only for access of other counters. Lifetime of class constructed this way must not exceed lifetime of existed one.
    TTabletCountersBase(const ui32 simpleOffset, const ui32 cumulativeOffset, const ui32 percentileOffset, TTabletCountersBase* counters)
        : SimpleCounters(counters->Simple().CountersHolder, counters->Simple().Counters + simpleOffset,
                         counters->Simple().Size() - simpleOffset)
        , CumulativeCounters(counters->Cumulative().CountersHolder, counters->Cumulative().Counters + cumulativeOffset,
                             counters->Cumulative().Size() - cumulativeOffset)
        , PercentileCounters(counters->Percentile().CountersHolder, counters->Percentile().Counters + percentileOffset,
                             counters->Percentile().Size() - percentileOffset)
        , SimpleCountersMetaInfo(counters->SimpleCountersMetaInfo + simpleOffset)
        , CumulativeCountersMetaInfo(counters->CumulativeCountersMetaInfo + cumulativeOffset)
        , PercentileCountersMetaInfo(counters->PercentileCountersMetaInfo + percentileOffset)

    {
        Y_VERIFY_DEBUG(counters->Simple().Size() > simpleOffset);
        Y_VERIFY_DEBUG(counters->Cumulative().Size() > cumulativeOffset);
        Y_VERIFY_DEBUG(counters->Percentile().Size() > percentileOffset);
    }

    virtual ~TTabletCountersBase()
    {}

    bool HasCounters() const {
        return SimpleCounters || CumulativeCounters || PercentileCounters;
    }

    // counters
    TCountersArray<TTabletSimpleCounter>& Simple() {
        return SimpleCounters;
    }
    const TCountersArray<TTabletSimpleCounter>& Simple() const {
        return SimpleCounters;
    }

    TCountersArray<TTabletCumulativeCounter>& Cumulative() {
        return CumulativeCounters;
    }
    const TCountersArray<TTabletCumulativeCounter>& Cumulative() const {
        return CumulativeCounters;
    }

    TCountersArray<TTabletPercentileCounter>& Percentile() {
        return PercentileCounters;
    }
    const TCountersArray<TTabletPercentileCounter>& Percentile() const {
        return PercentileCounters;
    }

    //
    TAutoPtr<TTabletCountersBase> MakeDiffForAggr(const TTabletCountersBase&) const;
    void RememberCurrentStateAsBaseline(/*out*/ TTabletCountersBase& baseLine) const;

    //
    void OutputHtml(IOutputStream &os) const;
    void OutputProto(NKikimrTabletBase::TTabletCountersBase& op) const;

    //
    const char* SimpleCounterName(ui32 index) const {
        return SimpleCountersMetaInfo[index];
    }

    const char* CumulativeCounterName(ui32 index) const {
        return CumulativeCountersMetaInfo[index];
    }

    const char* PercentileCounterName(ui32 index) const {
        return PercentileCountersMetaInfo[index];
    }

    void Populate(const TTabletCountersBase& rp) {
        if (!HasCounters()) {
            SimpleCounters.Reset(rp.SimpleCounters);
            SimpleCountersMetaInfo = rp.SimpleCountersMetaInfo;

            CumulativeCounters.Reset(rp.CumulativeCounters);
            CumulativeCountersMetaInfo = rp.CumulativeCountersMetaInfo;

            PercentileCounters.Reset(rp.PercentileCounters);
            PercentileCountersMetaInfo = rp.PercentileCountersMetaInfo;
        } else {
            SimpleCounters.Populate(rp.SimpleCounters);
            CumulativeCounters.Populate(rp.CumulativeCounters);
            PercentileCounters.Populate(rp.PercentileCounters);
        }
    }

private:
    //
    TTabletCountersBase(const TTabletCountersBase&);
    TTabletCountersBase& operator = (const TTabletCountersBase&);

    //
    template<typename T>
    void OutputHtml(IOutputStream &os, const char* sectionName, const char* const* counterNames, const char* counterClass, const TCountersArray<T>& counters) const;

    //
    TCountersArray<TTabletSimpleCounter> SimpleCounters;
    TCountersArray<TTabletCumulativeCounter> CumulativeCounters;
    TCountersArray<TTabletPercentileCounter> PercentileCounters;
    const char* const * SimpleCountersMetaInfo;
    const char* const * CumulativeCountersMetaInfo;
    const char* const * PercentileCountersMetaInfo;
};


////////////////////////////////////////////
/// The TTabletLabeledCountersBase class
////////////////////////////////////////////

//labeled counters are aggregated by Label across all tablets
//Id - identificator of tablet or whatever you want
//You can have severel counters for different labels inside one tablet

class TTabletLabeledCountersBase {
public:
    //
    enum EAggregateFunc {
        EAF_MAX = 1,
        EAF_MIN = 2,
        EAF_SUM = 3
    };

    TTabletLabeledCountersBase()
        : Counters(0)
        , Ids(0)
        , MetaInfo(nullptr)
        , Types(nullptr)
        , AggregateFunc(nullptr)
        , Group("")
        , GroupNames(nullptr)
        , Drop(false)
    {}

    //metaInfo - counters names
    //types - NKikimr::TLabeledCounterOptions::ECounterType
    //aggrFuncs - EAggreagteFunc-s casted to ui8
    //group - '/' separated list of group-values (user1/topic1/...)
    //groupNames - groups names (clientId,topic,...)
    //id - id for this user counter groups (tabletID or whatever, if there is several concurrent labeledCounters-generators inside one tablet)
    TTabletLabeledCountersBase(ui32 countersQnt,
        const char* const * metaInfo,
        const ui8* types,
        const ui8* aggrFunc,
        const TString& group, const char* const * groupNames, const ui64 id)
        : Counters(countersQnt)
        , Ids(countersQnt)
        , MetaInfo(metaInfo)
        , Types(types)
        , AggregateFunc(aggrFunc)
        , Group(group)
        , GroupNames(groupNames)
        , Drop(false)
    {
        for (ui32 i = 0; i < countersQnt; ++i)
            Ids[i].Set(id);
    }

    virtual ~TTabletLabeledCountersBase()
    {}

    bool HasCounters() const {
        return (bool)Counters;
    }

    // counters
    TCountersArray<TTabletSimpleCounter>& GetCounters() {
        return Counters;
    }

    const TCountersArray<TTabletSimpleCounter>& GetCounters() const {
        return Counters;
    }

    const TString& GetGroup() const {
        return Group;
    }


    void SetGroup(const TString& group) {
        Group = group;
    }

    const TCountersArray<TTabletSimpleCounter>& GetIds() const {
        return Ids;
    }

    TCountersArray<TTabletSimpleCounter>& GetIds() {
        return Ids;
    }

    ui8 GetCounterType(ui32 index) const {
        return Types[index];
    }

    const ui8* GetTypes() const {
        return Types;
    }

    const char * const * GetNames() const {
        return MetaInfo;
    }

    const ui8* GetAggrFuncs() const {
        return AggregateFunc;
    }

    void OutputHtml(IOutputStream &os) const;

    //
    const char* GetCounterName(ui32 index) const {
        return MetaInfo[index];
    }

    const char* GetGroupName(ui32 index) const {
        return GroupNames[index];
    }

    void SetDrop() {
        Drop = true;
    }

    bool GetDrop() const {
        return Drop;
    }

    //Counters will be filled with aggragated value by AggregateFunc, Ids will be filled with id from user counters with winning value
    void AggregateWith(const TTabletLabeledCountersBase& rp);

    TTabletLabeledCountersBase(const TTabletLabeledCountersBase&);
    TTabletLabeledCountersBase& operator = (const TTabletLabeledCountersBase&);

private:
    //
    TCountersArray<TTabletSimpleCounter> Counters;
    TCountersArray<TTabletSimpleCounter> Ids;
    const char* const * MetaInfo;
    const ui8* Types;
    const ui8* AggregateFunc;
    TString Group;
    const char* const * GroupNames;
    bool Drop;
};


IOutputStream& operator <<(IOutputStream& out, const TTabletLabeledCountersBase::EAggregateFunc& func);


} // end of NKikimr

