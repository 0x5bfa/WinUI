import std;
import winrt_base;
import winrt.Windows.Foundation;
import Xaml.Runtime;

int RunControlTests();

namespace
{
    using xaml::runtime::DependencyObject;
    using winrt::Windows::Foundation::IInspectable;
    using winrt::Windows::Foundation::IStringable;

    static_assert(!std::is_copy_constructible_v<DependencyObject>);
    static_assert(!std::is_move_constructible_v<DependencyObject>);

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
        {
            throw std::runtime_error(std::string(message));
        }
    }

    struct Counter : winrt::implements<Counter, DependencyObject, IStringable>
    {
        explicit Counter(std::atomic_int& destroyed) noexcept : m_destroyed(destroyed)
        {
        }

        ~Counter() override
        {
            ++m_destroyed;
        }

        void Increment()
        {
            VerifyAccess();
            ++m_value;
        }

        winrt::hstring ToString() const
        {
            return winrt::to_hstring(m_value);
        }

    private:
        std::atomic_int& m_destroyed;
        int m_value{};
    };

    void NativeAndABIShareIdentityAndState()
    {
        std::atomic_int destroyed{};
        auto object = winrt::make_self<Counter>(destroyed);
        auto base = object.as<DependencyObject>();
        auto inspectable = object.as<IInspectable>();
        auto stringable = object.as<IStringable>();

        Require(base.get() == static_cast<DependencyObject*>(object.get()), "Native base must be the same implementation.");
        Require(inspectable == stringable.as<IInspectable>(), "Interfaces must have one controlling identity.");
        constexpr winrt::guid agileObjectId{ "94ea2b94-e9cc-49e0-c0ff-ee64ca8f5b90" };
        winrt::Windows::Foundation::IUnknown agile{ nullptr };
        auto const agileResult = object->QueryInterface(agileObjectId, winrt::put_abi(agile));
        Require(agileResult >= 0 && agile, "WinUI runtime objects must advertise WinMD-mandated COM agility.");
        agile = nullptr;
        object->Increment();
        Require(stringable.ToString() == L"1", "ABI calls must observe native state.");

        object = nullptr;
        base = nullptr;
        inspectable = nullptr;
        Require(destroyed == 0, "A remaining ABI reference must keep the derived implementation alive.");
        stringable = nullptr;
        Require(destroyed == 1, "The derived implementation must be destroyed exactly once.");
    }

    void WeakReferencesFollowTheDerivedLifetime()
    {
        std::atomic_int destroyed{};
        auto object = winrt::make_self<Counter>(destroyed);
        auto weak = winrt::make_weak(object.as<IStringable>());
        auto resolved = weak.get();
        Require(static_cast<bool>(resolved), "A weak reference must resolve while the object is alive.");

        object = nullptr;
        Require(destroyed == 0, "A resolved weak reference must own the same object.");
        resolved = nullptr;
        Require(destroyed == 1, "A weak reference must not keep the object alive.");
        Require(!weak.get(), "A weak reference must not resurrect a destroyed object.");
    }

    void CrossThreadNativeAndABICallsAreRejected()
    {
        std::atomic_int destroyed{};
        auto object = winrt::make_self<Counter>(destroyed);
        auto stringable = object.as<IStringable>();
        Require(object->HasThreadAccess(), "The creating thread must own the object.");
        object->VerifyAccess();

        // The owner retains all references until the worker joins. This tests a
        // direct ABI entry, not COM apartment marshaling.
        std::exception_ptr workerFailure;
        std::jthread worker([&]
        {
            try
            {
                Require(!object->HasThreadAccess(), "Another thread must not own the object.");
                bool nativeRejected{};
                try
                {
                    object->Increment();
                }
                catch (winrt::hresult_wrong_thread const&)
                {
                    nativeRejected = true;
                }
                Require(nativeRejected, "Native operations must reject the wrong thread.");

                bool abiRejected{};
                try
                {
                    stringable.ToString();
                }
                catch (winrt::hresult_wrong_thread const&)
                {
                    abiRejected = true;
                }
                Require(abiRejected, "The inherited ABI guard must reject the wrong thread.");
            }
            catch (...)
            {
                workerFailure = std::current_exception();
            }
        });
        worker.join();
        if (workerFailure)
        {
            std::rethrow_exception(workerFailure);
        }
        Require(stringable.ToString() == L"0", "Rejected calls must leave native state unchanged.");
    }

    void ResourceNeutralFinalReleaseCanRunOffThread()
    {
        std::atomic_int destroyed{};
        auto object = winrt::make_self<Counter>(destroyed);
        auto weak = winrt::make_weak(object.as<IStringable>());
        std::jthread worker([owned = std::move(object)]() mutable
        {
            owned = nullptr;
        });
        worker.join();
        Require(destroyed == 1, "Final release must destroy a resource-neutral object exactly once.");
        Require(!weak.get(), "Off-thread final release must disconnect weak references.");
    }
}

int main()
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        NativeAndABIShareIdentityAndState();
        WeakReferencesFollowTheDerivedLifetime();
        CrossThreadNativeAndABICallsAreRejected();
        ResourceNeutralFinalReleaseCanRunOffThread();
        auto const controlTests = RunControlTests();
        std::cout << "4 DependencyObject ABI tests passed.\n";
        std::cout << controlTests << " control-kernel tests passed.\n";
        winrt::uninit_apartment();
        return 0;
    }
    catch (winrt::hresult_error const& error)
    {
        std::cerr << "WinRT failure: " << winrt::to_string(error.message()) << '\n';
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
    }
    return 1;
}
