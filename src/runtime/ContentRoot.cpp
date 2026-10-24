module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    ContentRoot::ContentRoot(winrt::com_ptr<UIElement> element, Size viewport,
        Enqueue dispatcher, Render renderer)
        : m_element(std::move(element)), m_viewport(viewport),
          m_enqueue(std::move(dispatcher)), m_renderer(std::move(renderer)),
          m_thread(std::this_thread::get_id())
    {
    }

    ContentRoot::~ContentRoot()
    {
        // This native host must be shut down on its creating UI thread.
        // Queued callbacks hold only a weak reference to the host.
        if (m_active && std::this_thread::get_id() == m_thread)
        {
            try { Shutdown(); } catch (...) {}
        }
    }

    std::shared_ptr<ContentRoot> ContentRoot::Create(winrt::com_ptr<UIElement> element,
        Size viewport, Enqueue dispatcher, Render renderer)
    {
        if (!element || !dispatcher || !renderer || !std::isfinite(viewport.width) ||
            !std::isfinite(viewport.height) || viewport.width < 0 || viewport.height < 0)
        {
            throw winrt::hresult_invalid_argument();
        }
        element->VerifyAccess();
        if (element->Parent() || element->m_rootInvalidated)
        {
            throw winrt::hresult_invalid_argument();
        }
        auto root = std::shared_ptr<ContentRoot>(new ContentRoot(std::move(element), viewport,
            std::move(dispatcher), std::move(renderer)));
        auto weak = std::weak_ptr<ContentRoot>(root);
        root->m_element->SetRootInvalidationCallback([weak]
        {
            if (auto owner = weak.lock()) { owner->Invalidate(); }
        }, root->m_enqueue, [weak]
        {
            if (auto owner = weak.lock()) { owner->UpdateLayout(); }
        });
        root->m_input = std::make_unique<InputManager>(*root->m_element);
        root->Invalidate();
        return root;
    }

    std::shared_ptr<ContentRoot> ContentRoot::CreateOnCurrentDispatcher(
        winrt::com_ptr<UIElement> element, Size viewport, Render renderer)
    {
        auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        if (!dispatcher) { throw winrt::hresult_illegal_method_call(); }
        return Create(std::move(element), viewport,
            [dispatcher](std::function<void()> callback)
            {
                return dispatcher.TryEnqueue([callback = std::move(callback)] { callback(); });
            }, std::move(renderer));
    }

    void ContentRoot::Invalidate()
    {
        if (std::this_thread::get_id() != m_thread) { throw winrt::hresult_wrong_thread(); }
        if (!m_active) { return; }
        ++m_invalidationVersion;
        if (!m_queued && !m_processing) { QueueFrame(); }
    }

    void ContentRoot::QueueFrame()
    {
        m_queued = true;
        auto weak = weak_from_this();
        try
        {
            if (!m_enqueue([weak]
                {
                    if (auto owner = weak.lock()) { owner->ProcessFrame(); }
                }))
            {
                m_queued = false;
                throw winrt::hresult_illegal_method_call();
            }
        }
        catch (...)
        {
            m_queued = false;
            throw;
        }
    }

    void ContentRoot::Resize(Size viewport)
    {
        if (std::this_thread::get_id() != m_thread) { throw winrt::hresult_wrong_thread(); }
        if (!m_active) { throw winrt::hresult_illegal_method_call(); }
        if (viewport.width < 0 || viewport.height < 0 ||
            !std::isfinite(viewport.width) || !std::isfinite(viewport.height))
        {
            throw winrt::hresult_invalid_argument();
        }
        if (m_viewport == viewport) { return; }
        m_viewport = viewport;
        m_element->InvalidateMeasure();
    }

    void ContentRoot::SetRasterizationScale(double scale)
    {
        if (std::this_thread::get_id() != m_thread) { throw winrt::hresult_wrong_thread(); }
        if (!m_active) { throw winrt::hresult_illegal_method_call(); }
        if (!std::isfinite(scale) || scale < std::numeric_limits<float>::epsilon() ||
            scale > std::numeric_limits<float>::max())
        {
            throw winrt::hresult_invalid_argument();
        }
        if (m_scale == scale) { return; }
        m_scale = scale;
        m_element->SetLayoutScale(scale);
    }

    double ContentRoot::RasterizationScale() const
    {
        if (std::this_thread::get_id() != m_thread) { throw winrt::hresult_wrong_thread(); }
        return m_scale;
    }

    void ContentRoot::ProcessFrame()
    {
        if (std::this_thread::get_id() != m_thread) { throw winrt::hresult_wrong_thread(); }
        if (!m_active || !m_queued || m_processing) { return; }
        m_queued = false;
        m_processing = true;
        try
        {
            m_input->ValidateState();
            if (!m_active) { m_processing = false; return; }
            auto element = m_element;
            auto renderer = m_renderer;
            // A bounded fixed-point pass prevents perpetual reentrant layout.
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                if (!m_active) { break; }
                element->Measure(m_viewport);
                if (!m_active) { break; }
                element->Arrange({ 0, 0, m_viewport.width, m_viewport.height });
                if (!m_active) { break; }
                element->DispatchLoaded();
                if (!m_active) { break; }
                auto const renderVersion = element->RenderVersion();
                if (!element->IsMeasureValid() || !element->IsArrangeValid()) { continue; }
                auto const invalidationVersion = m_invalidationVersion;
                renderer(*element);
                if (!m_active) { break; }
                if (element->IsMeasureValid() && element->IsArrangeValid() &&
                    element->RenderVersion() == renderVersion &&
                    invalidationVersion == m_invalidationVersion)
                {
                    ++m_frames;
                    m_processing = false;
                    return;
                }
            }
            m_processing = false;
            if (m_active) { QueueFrame(); }
        }
        catch (...)
        {
            m_processing = false;
            if (m_active && !m_queued) { QueueFrame(); }
            throw;
        }
    }

    void ContentRoot::UpdateLayout()
    {
        if (std::this_thread::get_id() != m_thread) { throw winrt::hresult_wrong_thread(); }
        if (!m_active || m_processing) { throw winrt::hresult_illegal_method_call(); }
        ProcessFrame();
    }

    void ContentRoot::Shutdown()
    {
        if (std::this_thread::get_id() != m_thread) { throw winrt::hresult_wrong_thread(); }
        if (!m_active) { return; }
        m_active = false;
        m_queued = false;
        std::exception_ptr failure;
        try { if (m_input) { m_input->Shutdown(); } }
        catch (...) { failure = std::current_exception(); }
        if (m_element)
        {
            try
            {
                m_element->SetRootInvalidationCallback({});
                m_element->SetLayoutScale(1.0);
            }
            catch (...) { if (!failure) { failure = std::current_exception(); } }
        }
        m_element = nullptr;
        m_enqueue = {};
        m_renderer = {};
        if (failure) { std::rethrow_exception(failure); }
    }

    InputManager& ContentRoot::Input()
    {
        if (std::this_thread::get_id() != m_thread) { throw winrt::hresult_wrong_thread(); }
        if (!m_active || !m_input) { throw winrt::hresult_illegal_method_call(); }
        return *m_input;
    }
}
