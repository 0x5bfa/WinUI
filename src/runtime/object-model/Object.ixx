export module Xaml.Runtime:ObjectModel;

import std;
import winrt_base;

export namespace xaml::runtime
{
    // One implementation owns both the native object and its WinRT identity.
    struct Object : winrt::implements<Object, winrt::Windows::Foundation::IInspectable, winrt::non_agile>
    {
        Object() noexcept;

        [[nodiscard]] bool HasThreadAccess() const noexcept;
        void VerifyAccess() const;

        // C++/WinRT's default ABI guard also uses this for derived interfaces.
        void abi_enter() const;

    private:
        std::thread::id const m_thread;
    };
}
