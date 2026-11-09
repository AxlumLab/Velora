#include "velora/platform/ime/windows/TsfTextInput.hpp"

#include <algorithm>
#include <atomic>
#include <climits>
#include <msctf.h>
#include <utility>

namespace velora
{

std::wstring toUtf16(std::string_view text)
{
    if (text.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::string toUtf8(std::wstring_view text)
{
    if (text.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}

namespace
{

template <class T> struct Com
{
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    Com(Com&& other) noexcept : p(std::exchange(other.p, nullptr)) {}
    ~Com()
    {
        reset();
    }
    void reset()
    {
        if (p)
        {
            p->Release();
            p = nullptr;
        }
    }
    T** put()
    {
        reset();
        return &p;
    }
    T* operator->() const
    {
        return p;
    }
    explicit operator bool() const
    {
        return p != nullptr;
    }
    template <class U> Com<U> as() const
    {
        Com<U> out;
        if (p)
            p->QueryInterface(__uuidof(U), reinterpret_cast<void**>(out.put()));
        return out;
    }
};

bool extent(ITfRange* range, LONG& start, LONG& length)
{
    ITfRangeACP* acp = nullptr;
    if (FAILED(range->QueryInterface(IID_ITfRangeACP, reinterpret_cast<void**>(&acp))))
        return false;
    const bool ok = SUCCEEDED(acp->GetExtent(&start, &length));
    acp->Release();
    return ok;
}

class TsfClient final : public ITfContextOwner,
                        public ITfContextOwnerCompositionSink,
                        public ITfTextEditSink,
                        public ITfEditSession
{
public:
    using Callbacks = TsfTextInput::Callbacks;
    TsfClient(HWND hwnd, Callbacks callbacks) : m_hwnd(hwnd), m_callbacks(std::move(callbacks)) {}

    bool initialize()
    {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(co))
            return false; // RPC_E_CHANGED_MODE: the thread is MTA
        m_coInitialized = true;
        if (FAILED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr,
                                    reinterpret_cast<void**>(m_threadMgr.put()))))
            return false;
        if (FAILED(m_threadMgr->Activate(&m_clientId)))
            return false;
        m_activated = true;
        if (FAILED(m_threadMgr->CreateDocumentMgr(m_docMgr.put())))
            return false;
        TfEditCookie cookie = 0;
        if (FAILED(m_docMgr->CreateContext(m_clientId, 0, static_cast<ITfContextOwnerCompositionSink*>(this),
                                           m_context.put(), &cookie)))
            return false;
        auto source = m_context.as<ITfSource>();
        if (!source ||
            FAILED(source->AdviseSink(IID_ITfContextOwner, static_cast<ITfContextOwner*>(this),
                                      &m_ownerCookie)) ||
            FAILED(
                source->AdviseSink(IID_ITfTextEditSink, static_cast<ITfTextEditSink*>(this), &m_editCookie)))
            return false;
        return SUCCEEDED(m_docMgr->Push(m_context.p));
    }

    void setEnabled(bool enabled)
    {
        if (!m_docMgr)
            return;
        Com<ITfDocumentMgr> previous;
        m_threadMgr->AssociateFocus(m_hwnd, enabled ? m_docMgr.p : nullptr, previous.put());
        if (enabled)
            m_threadMgr->SetFocus(m_docMgr.p);
        else if (auto services = m_context.as<ITfContextOwnerCompositionServices>())
            services->TerminateComposition(nullptr);
    }

    // Tears down TSF state and drops the owner's reference.
    void destroy()
    {
        if (m_context)
        {
            if (auto source = m_context.as<ITfSource>())
            {
                if (m_ownerCookie != TF_INVALID_COOKIE)
                    source->UnadviseSink(m_ownerCookie);
                if (m_editCookie != TF_INVALID_COOKIE)
                    source->UnadviseSink(m_editCookie);
            }
            m_ownerCookie = m_editCookie = TF_INVALID_COOKIE;
        }
        if (m_threadMgr && m_docMgr)
        {
            Com<ITfDocumentMgr> previous;
            m_threadMgr->AssociateFocus(m_hwnd, nullptr, previous.put());
        }
        if (m_docMgr)
            m_docMgr->Pop(TF_POPF_ALL);
        m_context.reset();
        m_docMgr.reset();
        if (m_activated)
            m_threadMgr->Deactivate();
        m_activated = false;
        m_threadMgr.reset();
        if (m_coInitialized)
            CoUninitialize();
        m_coInitialized = false;
        Release();
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override
    {
        if (!out)
            return E_INVALIDARG;
        *out = nullptr;
        if (riid == IID_IUnknown || riid == IID_ITfContextOwner)
            *out = static_cast<ITfContextOwner*>(this);
        else if (riid == IID_ITfContextOwnerCompositionSink)
            *out = static_cast<ITfContextOwnerCompositionSink*>(this);
        else if (riid == IID_ITfTextEditSink)
            *out = static_cast<ITfTextEditSink*>(this);
        else if (riid == IID_ITfEditSession)
            *out = static_cast<ITfEditSession*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ++m_refs;
    }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG n = --m_refs;
        if (n == 0)
            delete this;
        return n;
    }

    // ITfContextOwner
    STDMETHODIMP GetACPFromPoint(const POINT*, DWORD, LONG*) override
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetTextExt(LONG, LONG, RECT* rect, BOOL* clipped) override
    {
        *rect = m_callbacks.caretScreenRect();
        *clipped = FALSE;
        return S_OK;
    }
    STDMETHODIMP GetScreenExt(RECT* rect) override
    {
        return GetWindowRect(m_hwnd, rect) ? S_OK : E_FAIL;
    }
    STDMETHODIMP GetStatus(TF_STATUS* status) override
    {
        status->dwDynamicFlags = 0;
        status->dwStaticFlags = TF_SS_TRANSITORY; // composition-only document
        return S_OK;
    }
    STDMETHODIMP GetWnd(HWND* hwnd) override
    {
        *hwnd = m_hwnd;
        return S_OK;
    }
    STDMETHODIMP GetAttribute(REFGUID, VARIANT* value) override
    {
        value->vt = VT_EMPTY;
        return S_OK;
    }

    // ITfContextOwnerCompositionSink
    STDMETHODIMP OnStartComposition(ITfCompositionView*, BOOL* ok) override
    {
        *ok = TRUE;
        return S_OK;
    }
    STDMETHODIMP OnUpdateComposition(ITfCompositionView*, ITfRange*) override
    {
        return S_OK;
    }
    STDMETHODIMP OnEndComposition(ITfCompositionView*) override
    {
        return S_OK;
    }

    // ITfTextEditSink: read the result in a separate read-write session.
    STDMETHODIMP OnEndEdit(ITfContext*, TfEditCookie, ITfEditRecord*) override
    {
        HRESULT session = S_OK;
        m_context->RequestEditSession(m_clientId, static_cast<ITfEditSession*>(this),
                                      TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &session);
        return S_OK;
    }

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec) override
    {
        Com<ITfRange> all;
        if (FAILED(m_context->GetStart(ec, all.put())))
            return E_FAIL;
        LONG moved = 0;
        all->ShiftEnd(ec, LONG_MAX, &moved, nullptr);
        std::wstring text;
        wchar_t buffer[256];
        Com<ITfRange> cursor;
        all->Clone(cursor.put());
        while (true)
        {
            ULONG got = 0;
            if (FAILED(cursor->GetText(ec, TF_TF_MOVESTART, buffer, static_cast<ULONG>(std::size(buffer)),
                                       &got)) ||
                got == 0)
                break;
            text.append(buffer, got);
        }

        LONG compStart = LONG_MAX, compEnd = -1;
        if (auto composition = m_context.as<ITfContextComposition>())
        {
            Com<IEnumITfCompositionView> views;
            if (SUCCEEDED(composition->EnumCompositions(views.put())) && views)
            {
                for (Com<ITfCompositionView> view; views->Next(1, view.put(), nullptr) == S_OK;)
                {
                    Com<ITfRange> range;
                    LONG start = 0, length = 0;
                    if (SUCCEEDED(view->GetRange(range.put())) && extent(range.p, start, length))
                    {
                        compStart = std::min(compStart, start);
                        compEnd = std::max(compEnd, start + length);
                    }
                }
            }
        }
        const LONG size = static_cast<LONG>(text.size());
        if (compEnd < 0)
            compStart = compEnd = size; // nothing composing: everything is final
        compStart = std::clamp(compStart, 0L, size);
        compEnd = std::clamp(compEnd, compStart, size);

        const std::wstring committed = text.substr(0, compStart) + text.substr(compEnd);
        const std::wstring preedit = text.substr(compStart, compEnd - compStart);
        LONG caret = compEnd, selection = 0;
        TF_SELECTION sel{};
        ULONG fetched = 0;
        if (SUCCEEDED(m_context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched == 1)
        {
            LONG start = 0, length = 0;
            if (extent(sel.range, start, length))
            {
                caret = start;
                selection = length;
            }
            sel.range->Release();
        }
        caret = std::clamp(caret - compStart, 0L, static_cast<LONG>(preedit.size()));
        selection = std::clamp(selection, 0L, static_cast<LONG>(preedit.size()) - caret);

        if (!committed.empty())
        {
            // Drop finalized text so the store holds only the composition.
            if (compEnd < size)
                remove(ec, compEnd, size - compEnd);
            if (compStart > 0)
                remove(ec, 0, compStart);
            m_callbacks.commit(committed);
        }
        if (preedit != m_lastPreedit || caret != m_lastCaret || selection != m_lastSelection)
        {
            m_lastPreedit = preedit;
            m_lastCaret = caret;
            m_lastSelection = selection;
            m_callbacks.preedit(preedit, caret, selection);
        }
        return S_OK;
    }

private:
    void remove(TfEditCookie ec, LONG start, LONG length)
    {
        Com<ITfRange> range;
        if (FAILED(m_context->GetStart(ec, range.put())))
            return;
        if (auto acp = range.as<ITfRangeACP>(); acp && SUCCEEDED(acp->SetExtent(start, length)))
            range->SetText(ec, 0, nullptr, 0);
    }

    HWND m_hwnd;
    Callbacks m_callbacks;
    std::atomic<ULONG> m_refs{1};
    bool m_coInitialized = false, m_activated = false;
    Com<ITfThreadMgr> m_threadMgr;
    Com<ITfDocumentMgr> m_docMgr;
    Com<ITfContext> m_context;
    TfClientId m_clientId = TF_CLIENTID_NULL;
    DWORD m_ownerCookie = TF_INVALID_COOKIE, m_editCookie = TF_INVALID_COOKIE;
    std::wstring m_lastPreedit;
    LONG m_lastCaret = 0, m_lastSelection = 0;
};

} // namespace

namespace
{
class TsfHandle final : public TsfTextInput
{
public:
    explicit TsfHandle(TsfClient* client) : m_client(client) {}
    ~TsfHandle() override
    {
        destroy();
    }
    void setEnabled(bool enabled) override
    {
        if (m_client)
            m_client->setEnabled(enabled);
    }
    void destroy() override
    {
        if (m_client)
            m_client->destroy();
        m_client = nullptr;
    }

private:
    TsfClient* m_client; // COM object; released by destroy()
};
} // namespace

std::unique_ptr<TsfTextInput> TsfTextInput::create(HWND hwnd, Callbacks callbacks)
{
    auto* client = new TsfClient(hwnd, std::move(callbacks));
    if (!client->initialize())
    {
        client->destroy();
        return nullptr;
    }
    return std::make_unique<TsfHandle>(client);
}

} // namespace velora
