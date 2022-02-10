#pragma once

#include <util/system/defaults.h>
#include <util/generic/strbuf.h> 
#include <util/stream/output.h> 
#include <util/system/src_location.h> 
#include <util/system/yassert.h> 

 
// continues existing contexts chain 
 
#define YQL_LOG_CTX_SCOPE(...)  \ 
    auto Y_CAT(c, __LINE__) = ::NYql::NLog::MakeCtx(__VA_ARGS__); \ 
    Y_UNUSED(Y_CAT(c, __LINE__)) 
 
#define YQL_LOG_CTX_BLOCK(...) \ 
    if (auto Y_GENERATE_UNIQUE_ID(c) = ::NYql::NLog::MakeCtx(__VA_ARGS__)) { \ 
        goto Y_CAT(YQL_LOG_CTX_LABEL, __LINE__); \ 
    } else Y_CAT(YQL_LOG_CTX_LABEL, __LINE__): 
 
 
// starts new contexts chain, after leaving current scope restores 
// previous contexts chain 
 
#define YQL_LOG_CTX_ROOT_SCOPE(...)  \ 
    auto Y_CAT(c, __LINE__) = ::NYql::NLog::MakeRootCtx(__VA_ARGS__); \ 
    Y_UNUSED(Y_CAT(c, __LINE__)) 
 
#define YQL_LOG_CTX_ROOT_BLOCK(...) \ 
    if (auto Y_GENERATE_UNIQUE_ID(c) = ::NYql::NLog::MakeRootCtx(__VA_ARGS__)) { \ 
        goto Y_CAT(YQL_LOG_CTX_LABEL, __LINE__); \ 
    } else Y_CAT(YQL_LOG_CTX_LABEL, __LINE__): 
 
 
// adds current contexts path to exception message before throwing it 
 
#define YQL_LOG_CTX_THROW throw ::NYql::NLog::TYqlLogContextLocation(__LOCATION__) + 
 
class TLogElement; 
 
namespace NYql {
namespace NLog { 
namespace NImpl { 

/** 
 * @brief Represents item of logging context list. 
 */ 
struct TLogContextListItem { 
    TLogContextListItem* Next; 
    TLogContextListItem* Prev; 
    size_t NamesCount; 
 
    explicit TLogContextListItem(size_t namesCount = 0) 
        : Next(this) 
        , Prev(this) 
        , NamesCount(namesCount) 
    { 
    } 
 
    const TString* begin() const {
        auto* ptr = reinterpret_cast<const ui8*>(this); 
        return reinterpret_cast<const TString*>(ptr + sizeof(*this));
    } 
 
    const TString* end() const {
        return begin() + NamesCount; 
    } 
 
    bool HasNext() const { 
        return Next != this; 
    } 
 
    void LinkBefore(TLogContextListItem* item) { 
        Y_VERIFY_DEBUG(!HasNext()); 
        Next = item; 
        Prev = item->Prev; 
        Prev->Next = this; 
        Next->Prev = this; 
    } 
 
    void Unlink() { 
        if (!HasNext()) return; 
 
        Prev->Next = Next; 
        Next->Prev = Prev; 
        Next = Prev = this; 
    } 
}; 
 
/** 
 * @brief Returns pointer to thread local log context list. 
 */ 
TLogContextListItem* GetLogContextList(); 
 
} // namspace NImpl 
 
/** 
 * @brief YQL logger context element. Each element can contains several names. 
 */ 
template <size_t Size> 
class TLogContext: public NImpl::TLogContextListItem { 
public:
    template <typename... TArgs> 
    TLogContext(TArgs... args) 
        : TLogContextListItem(Size) 
        , Names_{{ TString{std::forward<TArgs>(args)}... }}
    { 
        LinkBefore(NImpl::GetLogContextList()); 
    } 
 
    ~TLogContext() { 
        Unlink(); 
    } 
 
    explicit inline operator bool() const noexcept {
        return true;
    }
 
private:
    std::array<TString, Size> Names_;
};

/** 
 * @brief Special Root context elements which replaces previous log context 
 *        list head by itself and restores previous one on destruction. 
 */ 
template <size_t Size> 
class TRootLogContext: public NImpl::TLogContextListItem { 
public: 
    template <typename... TArgs> 
    TRootLogContext(TArgs... args) 
        : TLogContextListItem(Size) 
        , Names_{{ TString{std::forward<TArgs>(args)}... }}
    { 
        NImpl::TLogContextListItem* ctxList = NImpl::GetLogContextList(); 
        PrevLogContextHead_.Prev = ctxList->Prev; 
        PrevLogContextHead_.Next = ctxList->Next; 
        ctxList->Next = ctxList->Prev = ctxList; 
        LinkBefore(ctxList); 
    } 
 
    ~TRootLogContext() { 
        Unlink(); 
        NImpl::TLogContextListItem* ctxList = NImpl::GetLogContextList(); 
        ctxList->Prev = PrevLogContextHead_.Prev; 
        ctxList->Next = PrevLogContextHead_.Next; 
    } 
 
    explicit inline operator bool() const noexcept { 
        return true; 
    } 
 
private: 
    std::array<TString, Size> Names_;
    NImpl::TLogContextListItem PrevLogContextHead_; 
}; 
 
/** 
 * @brief Helper function to construct TLogContext from variable 
 *        arguments list. 
 */ 
template <typename... TArgs> 
auto MakeCtx(TArgs&&... args) -> TLogContext<sizeof...(args)> { 
    return TLogContext<sizeof...(args)>(std::forward<TArgs>(args)...); 
} 
 
template <typename... TArgs> 
auto MakeRootCtx(TArgs&&... args) -> TRootLogContext<sizeof...(args)> { 
    return TRootLogContext<sizeof...(args)>(std::forward<TArgs>(args)...); 
} 
 
/** 
 * @brief Returns current logger contexts path as string. Each element 
 *        is separated with '/'. 
 */ 
TString CurrentLogContextPath();
 
/** 
 * @brief If last throwing exception was performed with YQL_LOG_CTX_THROW 
 *        macro this function returns location and context of that throw point. 
 */ 
TString ThrowedLogContextPath();
 
/** 
 * @brief Adds context preffix before logging message. 
 */ 
struct TContextPreprocessor { 
    static TAutoPtr<TLogElement> Preprocess(TAutoPtr<TLogElement> element); 
};

/** 
 * @brief Outputs current logger context into stream
 */
void OutputLogCtx(IOutputStream* out, bool withBraces);

/**
 * @brief Outputs current logger context into exception message. 
 */ 
class TYqlLogContextLocation { 
public: 
    TYqlLogContextLocation(const TSourceLocation& location) 
        : Location_(location.File, location.Line) 
    { 
    } 

    void SetThrowedLogContextPath() const; 

    template <class T> 
    inline T&& operator+(T&& t) { 
        SetThrowedLogContextPath(); 
        return std::forward<T>(t); 
    } 
 
private: 
    TSourceLocation Location_; 
}; 

} // namespace NLog 
} // namespace NYql 
