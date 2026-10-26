module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    DoubleAnimation::DoubleAnimation(DependencyObject* target, DependencyPropertyHandle property,
        double from, double to, std::chrono::milliseconds duration, AnimationEase ease)
        : m_property(std::move(property)), m_from(from), m_to(to), m_duration(duration), m_ease(ease)
    {
        if (!target || m_property.ValueType() != typeid(double) || !m_property.IsValidFor(*target) ||
            !std::isfinite(from) || !std::isfinite(to) || duration.count() < 0)
        {
            throw winrt::hresult_invalid_argument();
        }
        target->VerifyAccess();
        m_target = target->get_weak();
    }

    DoubleAnimation::~DoubleAnimation()
    {
        // Animation clocks are owned by the same UI thread as their target.
        Stop();
    }

    void DoubleAnimation::Completed(std::function<void()> callback)
    {
        if (auto target = m_target.get()) { target->VerifyAccess(); }
        m_completedCallback = std::move(callback);
    }

    void DoubleAnimation::Begin()
    {
        auto target = m_target.get();
        if (!target) { throw winrt::hresult_illegal_method_call(); }
        target->VerifyAccess();
        if (m_running || m_completed) { Stop(); }
        m_completed = false;
        if (m_duration.count() == 0)
        {
            target->SetAnimationValue(m_property, m_to);
            m_completed = true;
            if (auto callback = m_completedCallback) { callback(); }
            return;
        }
        auto queue = target->DispatcherQueue();
        if (!queue) { throw winrt::hresult_illegal_method_call(); }
        auto timer = queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(16));
        timer.IsRepeating(true);
        m_start = std::chrono::steady_clock::now();
        target->SetAnimationValue(m_property, m_from);
        m_running = true;
        try
        {
            auto weak = weak_from_this();
            m_tick = timer.Tick(winrt::auto_revoke, [weak](auto const&, auto const&)
            {
                if (auto animation = weak.lock()) { animation->Tick(); }
            });
            m_timer = std::move(timer);
            m_timer.Start();
        }
        catch (...) { Stop(); throw; }
    }

    void DoubleAnimation::Tick()
    {
        auto lifetime = shared_from_this();
        auto target = m_target.get();
        if (!target) { Stop(); return; }
        auto const elapsed = std::chrono::steady_clock::now() - m_start;
        double progress = std::clamp(std::chrono::duration<double>(elapsed).count() /
            std::chrono::duration<double>(m_duration).count(), 0.0, 1.0);
        double eased = progress;
        switch (m_ease)
        {
        case AnimationEase::EaseIn: eased = progress * progress; break;
        case AnimationEase::EaseOut: eased = 1.0 - (1.0 - progress) * (1.0 - progress); break;
        case AnimationEase::EaseInOut: eased = progress < 0.5 ? 2 * progress * progress :
            1 - 2 * (1 - progress) * (1 - progress); break;
        default: break;
        }
        target->SetAnimationValue(m_property, m_from + (m_to - m_from) * eased);
        if (progress < 1.0) { return; }
        m_timer.Stop();
        m_tick.revoke();
        m_timer = nullptr;
        m_running = false;
        m_completed = true;
        if (auto callback = m_completedCallback) { callback(); }
    }

    void DoubleAnimation::Stop()
    {
        if (m_timer) { m_timer.Stop(); }
        m_tick.revoke();
        m_timer = nullptr;
        if (m_running || m_completed)
        {
            if (auto target = m_target.get()) { target->ClearAnimationValue(m_property); }
        }
        m_running = false;
        m_completed = false;
    }

    void Storyboard::Add(std::shared_ptr<DoubleAnimation> animation)
    {
        if (!animation) { throw winrt::hresult_invalid_argument(); }
        m_animations.push_back(std::move(animation));
    }

    void Storyboard::Begin()
    {
        std::size_t started{};
        try
        {
            for (auto const& animation : m_animations) { animation->Begin(); ++started; }
        }
        catch (...)
        {
            while (started) { m_animations[--started]->Stop(); }
            throw;
        }
    }

    void Storyboard::Stop()
    {
        for (auto const& animation : m_animations) { animation->Stop(); }
    }
}
