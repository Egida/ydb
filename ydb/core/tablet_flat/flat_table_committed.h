#pragma once
#include "defs.h"

#include <ydb/core/base/row_version.h>
#include <util/generic/hash.h>
#include <util/generic/ptr.h>

#include <unordered_map>
#include <unordered_set>

namespace NKikimr {
namespace NTable {

    /**
     * An interface that maps TxId to commit RowVersion
     */
    class ITransactionMap : public TThrRefBase {
    public:
        /**
         * Returns a pointer to a stored row version for a given txId, or nullptr when it's missing
         */
        virtual const TRowVersion* Find(ui64 txId) const = 0;
    };

    /**
     * Smart pointer that can safely be used to call methods even when it's nullptr
     */
    class ITransactionMapPtr : public TIntrusivePtr<ITransactionMap> {
    public:
        using TIntrusivePtr::TIntrusivePtr;

        const TRowVersion* Find(ui64 txId) const {
            if (ITransactionMap* p = Get()) {
                return p->Find(txId);
            }
            return nullptr;
        }
    };

    /**
     * A simple pointer wrapper that may be useful as a function argument on a hot path
     *
     * Unlike a smart pointer it has no destructor and doesn't perform any Ref/UnRef
     */
    class ITransactionMapSimplePtr {
    public:
        ITransactionMapSimplePtr(const ITransactionMapPtr& ptr)
            : Ptr(ptr.Get())
        { }

        ITransactionMapSimplePtr(ITransactionMap* ptr)
            : Ptr(ptr)
        { }

        ITransactionMapSimplePtr(std::nullptr_t)
            : Ptr(nullptr)
        { }

        explicit operator bool() const {
            return bool(Ptr);
        }

        const TRowVersion* Find(ui64 txId) const {
            if (Ptr) {
                return Ptr->Find(txId);
            }
            return nullptr;
        }

    private:
        ITransactionMap* Ptr;
    };

    /**
     * A special implementation of transaction map with a single transaction
     */
    class TSingleTransactionMap final : public ITransactionMap {
    public:
        TSingleTransactionMap(ui64 txId, TRowVersion value)
            : TxId(txId)
            , Value(std::move(value))
        { }

        const TRowVersion* Find(ui64 txId) const override {
            if (TxId == txId) {
                return &Value;
            }
            return nullptr;
        }

        static ITransactionMapPtr Create(ui64 txId, TRowVersion value) {
            return new TSingleTransactionMap(txId, value);
        }

    private:
        ui64 TxId;
        TRowVersion Value;
    };

    /**
     * A special implementation of transaction map that merges two non-empty maps
     */
    class TMergedTransactionMap final : public ITransactionMap {
    private:
        TMergedTransactionMap(
                ITransactionMapPtr first,
                ITransactionMapPtr second)
            : First(std::move(first))
            , Second(std::move(second))
        { }

    public:
        const TRowVersion* Find(ui64 txId) const override {
            if (const TRowVersion* value = First.Find(txId)) {
                return value;
            }
            return Second.Find(txId);
        }

        static ITransactionMapPtr Create(
                const ITransactionMapPtr& first,
                const ITransactionMapPtr& second)
        {
            if (first) {
                if (second) {
                    return new TMergedTransactionMap(first, second);
                }
                return first;
            } else {
                return second;
            }
        }

    private:
        ITransactionMapPtr First;
        ITransactionMapPtr Second;
    };

    /**
     * A simple copy-on-write data structure for a TxId -> RowVersion map
     *
     * Pretends to be an instance of ITransactionMapPtr
     */
    class TTransactionMap {
    private:
        using TTxMap = std::unordered_map<ui64, TRowVersion>;

        struct TState final : public ITransactionMap, public TTxMap {
            const TRowVersion* Find(ui64 txId) const override {
                auto it = this->find(txId);
                if (it != this->end()) {
                    return &it->second;
                }
                return nullptr;
            }
        };

    public:
        using const_iterator = typename TState::const_iterator;

    public:
        TTransactionMap() = default;

        explicit operator bool() const {
            return State_ && !State_->empty();
        }

        const TRowVersion* Find(ui64 txId) const {
            if (State_) {
                return State_->Find(txId);
            }
            return nullptr;
        }

        void Clear() {
            State_.Reset();
        }

        void Add(ui64 txId, TRowVersion value) {
            Unshare()[txId] = value;
        }

        void Remove(ui64 txId) {
            if (State_ && State_->contains(txId)) {
                Unshare().erase(txId);
            }
        }

        operator ITransactionMapPtr() const {
            if (State_ && !State_->empty()) {
                return State_;
            }
            return nullptr;
        }

        operator ITransactionMapSimplePtr() const {
            return static_cast<ITransactionMap*>(State_.Get());
        }

        ITransactionMap* Get() const {
            return State_.Get();
        }

        ITransactionMap& operator*() const {
            Y_VERIFY(State_);
            return *State_;
        }

        ITransactionMap* operator->() const {
            Y_VERIFY(State_);
            return State_.Get();
        }

    public:
        const_iterator begin() const {
            if (State_) {
                const TState& state = *State_;
                return state.begin();
            } else {
                return { };
            }
        }

        const_iterator end() const {
            if (State_) {
                const TState& state = *State_;
                return state.end();
            } else {
                return { };
            }
        }

    private:
        TState& Unshare() {
            if (!State_) {
                State_ = MakeIntrusive<TState>();
            } else if (State_->RefCount() > 1) {
                State_ = MakeIntrusive<TState>(*State_);
            }
            return *State_;
        }

    private:
        TIntrusivePtr<TState> State_;
    };

    /**
     * A simple copy-on-write data structure for a TxId set
     */
    class TTransactionSet {
    private:
        using TTxSet = std::unordered_set<ui64>;

        struct TState : public TThrRefBase, TTxSet {
        };

    public:
        using const_iterator = TState::const_iterator;

    public:
        TTransactionSet() = default;

        explicit operator bool() const {
            return State_ && !State_->empty();
        }

        bool Contains(ui64 txId) const {
            return State_ && State_->contains(txId);
        }

        void Clear() {
            State_.Reset();
        }

        void Add(ui64 txId) {
            Unshare().insert(txId);
        }

        void Remove(ui64 txId) {
            if (State_ && State_->contains(txId)) {
                Unshare().erase(txId);
            }
        }

    public:
        const_iterator begin() const {
            if (State_) {
                const TState& state = *State_;
                return state.begin();
            } else {
                return { };
            }
        }

        const_iterator end() const {
            if (State_) {
                const TState& state = *State_;
                return state.end();
            } else {
                return { };
            }
        }

    private:
        TState& Unshare() {
            if (!State_) {
                State_ = MakeIntrusive<TState>();
            } else if (State_->RefCount() > 1) {
                State_ = MakeIntrusive<TState>(*State_);
            }
            return *State_;
        }

    private:
        TIntrusivePtr<TState> State_;
    };

} // namespace NTable
} // namespace NKikimr
