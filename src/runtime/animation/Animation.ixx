export module Xaml.Runtime:Animation;

import std;
import winrt_base;
import winrt.Microsoft.UI.Dispatching;
import :Properties;
import :DependencyObject;

export namespace xaml::runtime
{
    enum class AnimationEase { Linear, EaseIn, EaseOut, EaseInOut };

    // UI-thread animation clock. Values enter the existing animation property
    // precedence layer; a stopped clock releases its contribution.
    class DoubleAnimation final : public std::enable_shared_from_this<DoubleAnimation>
    {
    public:
        DoubleAnimation(DependencyObject* target, DependencyPropertyHandle property,
            double from, double to, std::chrono::milliseconds duration,
            AnimationEase ease = AnimationEase::Linear);
        ~DoubleAnimation();
        void Begin();
        void Stop();
        [[nodiscard]] bool IsRunning() const noexcept { return m_running; }
        [[nodiscard]] bool IsCompleted() const noexcept { return m_completed; }
        void Completed(std::function<void()> callback);

    private:
        void Tick();
        winrt::weak_ref<DependencyObject> m_target;
        DependencyPropertyHandle m_property;
        double m_from{}, m_to{};
        std::chrono::milliseconds m_duration{};
        std::chrono::steady_clock::time_point m_start{};
        AnimationEase m_ease{};
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker m_tick;
        std::function<void()> m_completedCallback;
        bool m_running{}, m_completed{};
    };

    class Storyboard final
    {
    public:
        void Add(std::shared_ptr<DoubleAnimation> animation);
        void Begin();
        void Stop();
    private:
        std::vector<std::shared_ptr<DoubleAnimation>> m_animations;
    };
}
