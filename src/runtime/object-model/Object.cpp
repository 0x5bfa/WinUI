module Xaml.Runtime;

import std;
import winrt_base;

namespace xaml::runtime
{
    Object::Object() noexcept : m_thread(std::this_thread::get_id())
    {
    }

    bool Object::HasThreadAccess() const noexcept
    {
        return m_thread == std::this_thread::get_id();
    }

    void Object::VerifyAccess() const
    {
        if (!HasThreadAccess())
        {
            throw winrt::hresult_wrong_thread();
        }
    }

    void Object::abi_enter() const
    {
        VerifyAccess();
    }
}
