#include <windows.h>
#include <msctf.h>
#include <textstor.h>
#include <olectl.h>
#include <winrt/base.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include "TextServicesBackend.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace
{
    namespace platform = xaml::runtime::platform;
    constexpr TsViewCookie viewCookie = 1;
    LONG Position(std::size_t value)
    {
        if (value > static_cast<std::size_t>(LONG_MAX)) { throw winrt::hresult_out_of_bounds(); }
        return static_cast<LONG>(value);
    }
    struct ThreadServices
    {
        winrt::com_ptr<ITfThreadMgr> manager;
        TfClientId client{};
        ThreadServices()
        {
            winrt::check_hresult(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(manager.put())));
            winrt::check_hresult(manager->Activate(&client));
        }
        ~ThreadServices() { if (manager) { (void)manager->Deactivate(); } }
        static std::shared_ptr<ThreadServices> Get()
        {
            thread_local std::weak_ptr<ThreadServices> current;
            if (auto owner = current.lock()) { return owner; }
            auto owner = std::make_shared<ThreadServices>(); current = owner; return owner;
        }
    };
    struct TextStore : winrt::implements<TextStore, ITextStoreACP, ITfContextOwnerCompositionSink, winrt::non_agile>
    {
        HWND window;
        platform::TextCallbacks callbacks;
        winrt::com_ptr<ITextStoreACPSink> sink;
        DWORD sinkMask{}, lock{}, pendingLock{};
        bool active{ true }, queued{}, editing{}, notifying{}, selectionPending{}, layoutPending{}, composing{};
        double scale{ 1 };
        std::vector<TS_TEXTCHANGE> changes;
        std::thread::id thread{ std::this_thread::get_id() };
        winrt::Microsoft::UI::Dispatching::DispatcherQueue queue{ nullptr };

        TextStore(HWND inputWindow, platform::TextCallbacks value, double rasterScale)
            : window(inputWindow), callbacks(std::move(value)), scale(rasterScale),
              queue(winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread()) {}
        template <typename F> HRESULT Guard(F&& action) noexcept
        {
            try
            {
                auto lifetime = get_strong();
                if (thread != std::this_thread::get_id()) { return RPC_E_WRONG_THREAD; }
                if (!active) { return TS_E_NOLOCK; }
                return action();
            }
            catch (...) { return winrt::to_hresult(); }
        }
        void RequireLock(bool write = false)
        {
            if (!lock || (write && (lock & TS_LF_READWRITE) != TS_LF_READWRITE)) { winrt::throw_hresult(TS_E_NOLOCK); }
            if (write && callbacks.readOnly()) { winrt::throw_hresult(TS_E_READONLY); }
        }
        LONG Length() const { return Position(callbacks.text().size()); }
        void Range(LONG start, LONG end)
        {
            if (start < 0 || end < start || end > Length()) { winrt::throw_hresult(TS_E_INVALIDPOS); }
        }
        HRESULT Grant(DWORD flags)
        {
            auto lifetime = get_strong();
            auto listener = sink;
            lock = flags & TS_LF_READWRITE;
            auto result = listener->OnLockGranted(flags);
            lock = 0;
            Schedule();
            return result;
        }
        void Schedule()
        {
            if (queued || !active || !sink || (!pendingLock && changes.empty() && !selectionPending && !layoutPending)) { return; }
            queued = true;
            auto self = get_strong();
            if (!queue || !queue.TryEnqueue([self]
            {
                self->queued = false;
                (void)self->Guard([&]() -> HRESULT
                {
                    if (self->lock || self->notifying) { self->Schedule(); return S_OK; }
                    auto listener = self->sink;
                    if (!listener) { return S_OK; }
                    self->notifying = true;
                    auto changes = std::exchange(self->changes, {});
                    bool const selection = std::exchange(self->selectionPending, false);
                    bool const layout = std::exchange(self->layoutPending, false);
                    for (auto const& change : changes)
                    {
                        if (self->active && self->sink == listener && (self->sinkMask & TS_AS_TEXT_CHANGE))
                        { (void)listener->OnTextChange(0, &change); }
                    }
                    if (self->active && self->sink == listener && selection && (self->sinkMask & TS_AS_SEL_CHANGE))
                    { (void)listener->OnSelectionChange(); }
                    if (self->active && self->sink == listener && layout && (self->sinkMask & TS_AS_LAYOUT_CHANGE))
                    { (void)listener->OnLayoutChange(TS_LC_CHANGE, viewCookie); }
                    self->notifying = false;
                    if (self->active && self->sink && self->pendingLock)
                    { (void)self->Grant(std::exchange(self->pendingLock, 0)); }
                    self->Schedule();
                    return S_OK;
                });
            })) { queued = false; pendingLock = 0; }
        }
        HRESULT __stdcall AdviseSink(REFIID iid, IUnknown* object, DWORD mask) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                if (iid != __uuidof(ITextStoreACPSink)) { return E_INVALIDARG; }
                if (!object) { return E_POINTER; }
                auto listener = winrt::com_ptr<ITextStoreACPSink>{};
                winrt::check_hresult(object->QueryInterface(IID_PPV_ARGS(listener.put())));
                if (sink && sink.as<IUnknown>() != listener.as<IUnknown>()) { return CONNECT_E_ADVISELIMIT; }
                sink = std::move(listener); sinkMask = mask; return S_OK;
            });
        }
        HRESULT __stdcall UnadviseSink(IUnknown* object) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                if (!object) { return E_POINTER; }
                winrt::com_ptr<IUnknown> identity;
                winrt::check_hresult(object->QueryInterface(IID_PPV_ARGS(identity.put())));
                if (!sink || sink.as<IUnknown>() != identity) { return CONNECT_E_NOCONNECTION; }
                sink = nullptr; sinkMask = 0; pendingLock = 0; return S_OK;
            });
        }
        HRESULT __stdcall RequestLock(DWORD flags, HRESULT* session) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                if (!session) { return E_POINTER; }
                if (!sink) { return E_UNEXPECTED; }
                if (!(flags & TS_LF_READ) || (flags & ~(TS_LF_SYNC | TS_LF_READWRITE))) { return E_INVALIDARG; }
                if (lock || notifying || !changes.empty() || selectionPending || layoutPending)
                {
                    if (flags & TS_LF_SYNC) { *session = TS_E_SYNCHRONOUS; return S_OK; }
                    pendingLock |= flags & TS_LF_READWRITE; *session = TS_S_ASYNC; Schedule(); return S_OK;
                }
                pendingLock = 0; *session = Grant(flags); return S_OK;
            });
        }
        HRESULT __stdcall GetStatus(TS_STATUS* status) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                if (!status) { return E_POINTER; }
                *status = { callbacks.readOnly() ? TS_SD_READONLY : 0u, TS_SS_NOHIDDENTEXT }; return S_OK;
            });
        }
        HRESULT __stdcall QueryInsert(LONG start, LONG end, ULONG count, LONG* resultStart, LONG* resultEnd) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                if (!resultStart || !resultEnd) { return E_POINTER; }
                Range(start, end); (void)Position(static_cast<std::size_t>(Length() - (end - start)) + count);
                *resultStart = start; *resultEnd = end; return S_OK;
            });
        }
        HRESULT __stdcall GetSelection(ULONG index, ULONG count, TS_SELECTION_ACP* selection, ULONG* fetched) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                if (!fetched || (count && !selection)) { return E_POINTER; }
                *fetched = 0; RequireLock();
                if (index != TS_DEFAULT_SELECTION && index != 0) { return TS_E_NOSELECTION; }
                if (count)
                {
                    auto value = callbacks.selection();
                    Range(Position(value.start), Position(value.end));
                    *selection = { Position(value.start), Position(value.end), { value.activeAtStart ? TS_AE_START : TS_AE_END, FALSE } };
                    *fetched = 1;
                }
                return S_OK;
            });
        }
        HRESULT __stdcall SetSelection(ULONG count, TS_SELECTION_ACP const* selection) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                RequireLock(true);
                if (count != 1 || !selection) { return E_INVALIDARG; }
                Range(selection->acpStart, selection->acpEnd);
                editing = true;
                try { callbacks.select({ static_cast<std::uint32_t>(selection->acpStart), static_cast<std::uint32_t>(selection->acpEnd), selection->style.ase == TS_AE_START }); }
                catch (...) { editing = false; throw; }
                editing = false; return S_OK;
            });
        }
        HRESULT __stdcall GetText(LONG start, LONG end, WCHAR* text, ULONG requested, ULONG* returned,
            TS_RUNINFO* runs, ULONG runCapacity, ULONG* runCount, LONG* next) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                if (!returned || !runCount || !next || (requested && !text) || (runCapacity && !runs)) { return E_POINTER; }
                *returned = 0; *runCount = 0; *next = start; RequireLock();
                auto value = callbacks.text(); LONG length = Position(value.size());
                if (end == -1) { end = length; }
                if (start < 0 || end < start || end > length) { return TS_E_INVALIDPOS; }
                ULONG const count = requested ? std::min(requested, static_cast<ULONG>(end - start)) :
                    runCapacity ? static_cast<ULONG>(end - start) : 0;
                if (requested && count) { std::copy_n(value.data() + start, count, text); *returned = count; }
                if (runCapacity && count) { runs[0] = { count, TS_RT_PLAIN }; *runCount = 1; }
                *next = start + static_cast<LONG>(count); return S_OK;
            });
        }
        HRESULT __stdcall SetText(DWORD, LONG start, LONG end, WCHAR const* text, ULONG count, TS_TEXTCHANGE* change) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                RequireLock(true); if (!change || (count && !text)) { return E_POINTER; }
                Range(start, end);
                auto newEnd = Position(static_cast<std::size_t>(start) + count);
                (void)Position(static_cast<std::size_t>(Length() - (end - start)) + count);
                editing = true;
                try { callbacks.replace(static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(end), { text ? text : L"", count }); }
                catch (...) { editing = false; throw; }
                editing = false; *change = { start, end, newEnd }; return S_OK;
            });
        }
        HRESULT __stdcall GetFormattedText(LONG, LONG, IDataObject** result) noexcept override
        { if (!result) { return E_POINTER; } *result = nullptr; return E_NOTIMPL; }
        HRESULT __stdcall GetEmbedded(LONG, REFGUID, REFIID, IUnknown** result) noexcept override
        { if (!result) { return E_POINTER; } *result = nullptr; return TS_E_NOOBJECT; }
        HRESULT __stdcall QueryInsertEmbedded(GUID const*, FORMATETC const*, BOOL* result) noexcept override
        { if (!result) { return E_POINTER; } *result = FALSE; return S_OK; }
        HRESULT __stdcall InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) noexcept override { return TS_E_FORMAT; }
        HRESULT __stdcall InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*, TS_TEXTCHANGE*) noexcept override { return TS_E_FORMAT; }
        HRESULT __stdcall InsertTextAtSelection(DWORD flags, WCHAR const* text, ULONG count, LONG* start, LONG* end, TS_TEXTCHANGE* change) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                RequireLock(!(flags & TS_IAS_QUERYONLY));
                auto selection = callbacks.selection();
                if (!(flags & TS_IAS_NOQUERY) && (!start || !end)) { return E_POINTER; }
                auto first = Position(selection.start), last = Position(selection.end);
                Range(first, last);
                LONG insertedEnd = Position(static_cast<std::size_t>(first) + count);
                if (!(flags & TS_IAS_NOQUERY)) { *start = first; *end = insertedEnd; }
                if (flags & TS_IAS_QUERYONLY) { return S_OK; }
                TS_TEXTCHANGE result{};
                winrt::check_hresult(SetText(0, first, last, text, count, &result));
                TS_SELECTION_ACP selected{ insertedEnd, insertedEnd, { TS_AE_END, FALSE } };
                winrt::check_hresult(SetSelection(1, &selected));
                if (change) { *change = result; } return S_OK;
            });
        }
        HRESULT __stdcall RequestSupportedAttrs(DWORD, ULONG, TS_ATTRID const*) noexcept override { return S_OK; }
        HRESULT __stdcall RequestAttrsAtPosition(LONG, ULONG, TS_ATTRID const*, DWORD) noexcept override { return S_OK; }
        HRESULT __stdcall RequestAttrsTransitioningAtPosition(LONG, ULONG, TS_ATTRID const*, DWORD) noexcept override { return S_OK; }
        HRESULT __stdcall FindNextAttrTransition(LONG start, LONG halt, ULONG, TS_ATTRID const*, DWORD,
            LONG* next, BOOL* found, LONG* offset) noexcept override
        {
            if (!next || !found || !offset) { return E_POINTER; }
            *next = halt; *found = FALSE; *offset = 0; return start >= 0 && halt >= start ? S_OK : TS_E_INVALIDPOS;
        }
        HRESULT __stdcall RetrieveRequestedAttrs(ULONG, TS_ATTRVAL*, ULONG* fetched) noexcept override
        { if (!fetched) { return E_POINTER; } *fetched = 0; return S_OK; }
        HRESULT __stdcall GetEndACP(LONG* end) noexcept override
        { return Guard([&]() -> HRESULT { if (!end) { return E_POINTER; } RequireLock(); *end = Length(); return S_OK; }); }
        HRESULT __stdcall GetActiveView(TsViewCookie* view) noexcept override
        { if (!view) { return E_POINTER; } *view = viewCookie; return S_OK; }
        HRESULT __stdcall GetACPFromPoint(TsViewCookie view, POINT const* point, DWORD, LONG* result) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                if (!point || !result) { return E_POINTER; } RequireLock(); if (view != viewCookie) { return E_INVALIDARG; }
                auto local = *point; winrt::check_bool(ScreenToClient(window, &local));
                *result = Position(callbacks.position(local.x / scale, local.y / scale)); return S_OK;
            });
        }
        HRESULT __stdcall GetTextExt(TsViewCookie view, LONG start, LONG end, RECT* bounds, BOOL* clipped) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                if (!bounds || !clipped) { return E_POINTER; } RequireLock();
                if (view != viewCookie) { return E_INVALIDARG; } Range(start, end);
                auto value = callbacks.bounds(static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(end));
                if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.width) || !std::isfinite(value.height) ||
                    value.width < 0 || value.height < 0) { return TS_E_NOLAYOUT; }
                POINT origin{}; winrt::check_bool(ClientToScreen(window, &origin));
                auto pixel = [](double dip) -> LONG
                {
                    if (dip < LONG_MIN || dip > LONG_MAX) { winrt::throw_hresult(TS_E_NOLAYOUT); }
                    return static_cast<LONG>(dip);
                };
                *bounds = { pixel(std::floor(value.x * scale) + origin.x), pixel(std::floor(value.y * scale) + origin.y),
                    pixel(std::ceil((value.x + value.width) * scale) + origin.x), pixel(std::ceil((value.y + value.height) * scale) + origin.y) };
                RECT screen{}; winrt::check_hresult(GetScreenExt(view, &screen));
                *clipped = bounds->left < screen.left || bounds->top < screen.top || bounds->right > screen.right || bounds->bottom > screen.bottom;
                return S_OK;
            });
        }
        HRESULT __stdcall GetScreenExt(TsViewCookie view, RECT* bounds) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                if (!bounds) { return E_POINTER; } if (view != viewCookie) { return E_INVALIDARG; }
                winrt::check_bool(GetClientRect(window, bounds)); POINT origin{};
                winrt::check_bool(ClientToScreen(window, &origin)); OffsetRect(bounds, origin.x, origin.y); return S_OK;
            });
        }
        HRESULT __stdcall GetWnd(TsViewCookie view, HWND* result) noexcept override
        { if (!result) { return E_POINTER; } *result = window; return view == viewCookie ? S_OK : E_INVALIDARG; }
        void Composition(ITfRange* range, bool enabled)
        {
            auto acp = winrt::com_ptr<ITfRangeACP>{};
            winrt::check_hresult(range->QueryInterface(IID_PPV_ARGS(acp.put())));
            LONG start{}, count{}; winrt::check_hresult(acp->GetExtent(&start, &count));
            Range(start, Position(static_cast<std::size_t>(start) + count));
            callbacks.composition(static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(start + count), enabled);
            composing = enabled;
        }
        HRESULT __stdcall OnStartComposition(ITfCompositionView* view, BOOL* accepted) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                if (!view || !accepted) { return E_POINTER; } *accepted = FALSE;
                if (callbacks.readOnly() || composing) { return S_OK; }
                winrt::com_ptr<ITfRange> range; winrt::check_hresult(view->GetRange(range.put()));
                Composition(range.get(), true); *accepted = TRUE; return S_OK;
            });
        }
        HRESULT __stdcall OnUpdateComposition(ITfCompositionView* view, ITfRange* proposed) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                if (!view) { return E_POINTER; } winrt::com_ptr<ITfRange> range;
                if (proposed) { range.copy_from(proposed); } else { winrt::check_hresult(view->GetRange(range.put())); }
                Composition(range.get(), true); return S_OK;
            });
        }
        HRESULT __stdcall OnEndComposition(ITfCompositionView* view) noexcept override
        {
            return Guard([&]() -> HRESULT
            {
                if (!view) { return E_POINTER; } winrt::com_ptr<ITfRange> range;
                winrt::check_hresult(view->GetRange(range.put())); Composition(range.get(), false); return S_OK;
            });
        }
    };
    struct Session
    {
        HWND window;
        std::thread::id thread{ std::this_thread::get_id() };
        std::shared_ptr<ThreadServices> services;
        winrt::com_ptr<ITfDocumentMgr> document;
        winrt::com_ptr<ITfContext> context;
        winrt::com_ptr<TextStore> store;
        double scale{ 1 };
        bool closed{};
        explicit Session(HWND value) : window(value) {}
        void Verify() const
        {
            if (thread != std::this_thread::get_id()) { throw winrt::hresult_wrong_thread(); }
            if (closed) { throw winrt::hresult_illegal_method_call(); }
        }
        void Clear() noexcept
        {
            auto oldContext = std::exchange(context, {});
            auto oldDocument = std::exchange(document, {});
            auto oldStore = std::exchange(store, {});
            if (oldContext)
            {
                if (auto owner = oldContext.try_as<ITfContextOwnerCompositionServices>()) { (void)owner->TerminateComposition(nullptr); }
            }
            if (oldStore) { oldStore->active = false; oldStore->sink = nullptr; }
            if (services && oldDocument)
            {
                winrt::com_ptr<ITfDocumentMgr> current;
                if (SUCCEEDED(services->manager->GetFocus(current.put())) && current == oldDocument)
                { (void)services->manager->SetFocus(nullptr); }
                winrt::com_ptr<ITfDocumentMgr> previous;
                (void)services->manager->AssociateFocus(window, nullptr, previous.put());
            }
            if (oldDocument) { (void)oldDocument->Pop(TF_POPF_ALL); }
        }
        ~Session() { Clear(); }
    };
    using SessionHandle = std::shared_ptr<Session>;
    SessionHandle GetSession(void* handle) { return *static_cast<SessionHandle*>(handle); }
}

void* XamlTextServicesCreate(std::uintptr_t window)
{
    if (!IsWindow(reinterpret_cast<HWND>(window))) { throw winrt::hresult_invalid_argument(); }
    return new SessionHandle(std::make_shared<Session>(reinterpret_cast<HWND>(window)));
}
void XamlTextServicesFocus(void* handle, platform::TextCallbacks callbacks)
{
    auto owner = GetSession(handle); auto& session = *owner; session.Verify(); session.Clear();
    if (session.closed) { return; }
    if (!callbacks.text) { return; }
    if (!session.services) { session.services = ThreadServices::Get(); }
    session.store = winrt::make_self<TextStore>(session.window, std::move(callbacks), session.scale);
    try
    {
        winrt::check_hresult(session.services->manager->CreateDocumentMgr(session.document.put()));
        TfEditCookie cookie{};
        winrt::check_hresult(session.document->CreateContext(session.services->client, 0,
            static_cast<ITextStoreACP*>(session.store.get()), session.context.put(), &cookie));
        if (session.closed) { session.Clear(); return; }
        winrt::check_hresult(session.document->Push(session.context.get()));
        if (session.closed) { session.Clear(); return; }
        winrt::com_ptr<ITfDocumentMgr> previous;
        winrt::check_hresult(session.services->manager->AssociateFocus(session.window, session.document.get(), previous.put()));
        winrt::check_hresult(session.services->manager->SetFocus(session.document.get()));
    }
    catch (...) { session.Clear(); throw; }
}
void XamlTextServicesChanged(void* handle, std::uint32_t start, std::uint32_t oldEnd, std::uint32_t newEnd)
{
    auto owner = GetSession(handle); auto& session = *owner; session.Verify();
    if (auto store = session.store; store && !store->editing)
    {
        if (oldEnd < start || newEnd < start) { throw winrt::hresult_invalid_argument(); }
        store->changes.push_back({ Position(start), Position(oldEnd), Position(newEnd) });
        store->selectionPending = true; store->layoutPending = true; store->Schedule();
    }
}
void XamlTextServicesSelectionChanged(void* handle)
{
    auto owner = GetSession(handle); auto& session = *owner; session.Verify();
    if (auto store = session.store; store && !store->editing) { store->selectionPending = true; store->Schedule(); }
}
void XamlTextServicesLayoutChanged(void* handle, double scale)
{
    auto owner = GetSession(handle); auto& session = *owner; session.Verify();
    if (!std::isfinite(scale) || scale <= 0) { throw winrt::hresult_invalid_argument(); }
    session.scale = scale;
    if (auto store = session.store) { store->scale = scale; store->layoutPending = true; store->Schedule(); }
}
bool XamlTextServicesKey(void* handle, std::uint32_t message, std::uintptr_t wparam, std::intptr_t lparam)
{
    auto owner = GetSession(handle); auto& session = *owner; session.Verify();
    if (!session.services || !session.store) { return false; }
    winrt::com_ptr<ITfDocumentMgr> focused;
    winrt::check_hresult(session.services->manager->GetFocus(focused.put()));
    if (focused != session.document) { return false; }
    bool const down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    if (!down && message != WM_KEYUP && message != WM_SYSKEYUP) { return false; }
    auto keys = session.services->manager.as<ITfKeystrokeMgr>(); BOOL eaten{};
    winrt::check_hresult(down ? keys->TestKeyDown(wparam, lparam, &eaten) : keys->TestKeyUp(wparam, lparam, &eaten));
    if (!eaten) { return false; }
    winrt::check_hresult(down ? keys->KeyDown(wparam, lparam, &eaten) : keys->KeyUp(wparam, lparam, &eaten));
    return eaten != FALSE;
}
void XamlTextServicesDestroy(void* handle)
{
    std::unique_ptr<SessionHandle> handleOwner(static_cast<SessionHandle*>(handle));
    auto session = *handleOwner; session->Verify(); session->closed = true; session->Clear();
}
