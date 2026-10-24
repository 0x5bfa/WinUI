import std;
import winrt_base;
import winrt.Windows.Foundation;
import Xaml.Runtime;
import Xaml.Controls;

namespace
{
    using namespace xaml::runtime;
    using namespace xaml::controls;

    void Require(bool condition, std::string_view message)
    {
        if (!condition) { throw std::runtime_error(std::string(message)); }
    }

    template <typename Error = winrt::hresult_invalid_argument, typename Action>
    void Rejects(Action action)
    {
        bool rejected{};
        try { action(); }
        catch (Error const&) { rejected = true; }
        Require(rejected, "The invalid operation was accepted.");
    }

    struct MeasuredElement : winrt::implements<MeasuredElement, FrameworkElement>
    {
        int measures{};
        int arranges{};
        bool invalidateDuringMeasure{};
        bool throwDuringMeasure{};
        bool recurseDuringMeasure{};
        bool invalidResult{};
        Size lastAvailable{};
        std::function<void()> measureCallback;

        Size MeasureOverride(Size available) override
        {
            ++measures;
            lastAvailable = available;
            if (throwDuringMeasure) { throw std::runtime_error("Measure failed."); }
            if (recurseDuringMeasure) { Measure({ 100, 50 }); }
            if (invalidateDuringMeasure) { InvalidateMeasure(); }
            if (invalidResult) { return { std::numeric_limits<double>::infinity(), 10 }; }
            if (measureCallback) { measureCallback(); }
            return { 20, 10 };
        }

        Size ArrangeOverride(Size size) override
        {
            ++arranges;
            return size;
        }
    };

    struct HitTestOverlay : winrt::implements<HitTestOverlay, FrameworkElement>
    {
        void Add(UIElement* first, UIElement* second)
        {
            UIElement* children[]{ first, second };
            ReplaceVisualChildren(children);
        }

        Size MeasureOverride(Size available) override
        {
            for (std::size_t i = 0; i < VisualChildrenCount(); ++i)
            {
                VisualChildAt(i)->Measure(available);
            }
            return available;
        }

        Size ArrangeOverride(Size finalSize) override
        {
            for (std::size_t i = 0; i < VisualChildrenCount(); ++i)
            {
                VisualChildAt(i)->Arrange({ 0, 0, finalSize.width, finalSize.height });
            }
            return finalSize;
        }
    };

    struct ObservedObject : winrt::implements<ObservedObject, DependencyObject>
    {
        static DependencyProperty<int> const ValueProperty;
        int notifications{};
        bool reenter{};
        bool throwInNotification{};
        std::function<void()> changed;

        void OnPropertyChanged(PropertyChange const&) override
        {
            ++notifications;
            if (reenter && GetValue(ValueProperty) == 1) { SetValue(ValueProperty, 2); }
            if (throwInNotification) { throw std::runtime_error("Notification failed."); }
            if (changed) { changed(); }
        }
    };

    DependencyProperty<int> const ObservedObject::ValueProperty =
        DependencyProperty<int>::Register<ObservedObject>("Value", 0, PropertyEffects::None,
            +[](int value) { return value >= 0; });

    struct ReferenceValue : winrt::implements<ReferenceValue, winrt::Windows::Foundation::IStringable>
    {
        explicit ReferenceValue(std::atomic_int& disposed) : m_disposed(disposed) {}
        ~ReferenceValue() { ++m_disposed; }
        winrt::hstring ToString() const { return L"reference"; }

    private:
        std::atomic_int& m_disposed;
    };

    void WinRTReferencePropertiesRetainAndReleaseValues()
    {
        using Inspectable = winrt::Windows::Foundation::IInspectable;
        std::atomic_int disposed{};
        auto property = DependencyProperty<Inspectable>::Register<DependencyObject>("Reference", nullptr);
        auto object = winrt::make_self<DependencyObject>();
        auto value = winrt::make_self<ReferenceValue>(disposed);
        auto weak = winrt::make_weak(value.as<winrt::Windows::Foundation::IStringable>());
        object->SetBoxedValue(property.Erase(), value.as<Inspectable>());
        Require(object->GetValue(property) == value.as<Inspectable>() &&
            object->ReadLocalValue(property) == value.as<Inspectable>() &&
            object->GetBoxedValue(property.Erase()) == value.as<Inspectable>(),
            "Object-valued property storage must retain the original COM identity.");
        value = nullptr;
        Require(weak.get() && disposed == 0, "Local storage must retain its value after its caller releases it.");
        object->ClearValue(property);
        Require(!weak.get() && disposed == 1, "Clearing a reference-valued property must release the COM edge.");
        Require(!object->GetValue(property) && !object->ReadLocalValue(property),
            "Clearing a reference property must restore its null default and unset its local value.");
    }

    void MetadataFactoriesAreLazyScopedAndValidated()
    {
        int creations{};
        int changes{};
        auto property = DependencyProperty<int>::RegisterWithMetadata<DependencyObject>(
            "GeneratedDefault", PropertyMetadata<int>(
                PropertyMetadata<int>::DefaultFactory([&](DependencyObject&)
                {
                    return ++creations * 10;
                }),
                [&](DependencyObject&, int before, int after)
                {
                    Require((before == 10 && after == 99) || (before == 99 && after == 10),
                        "Metadata changes must use the owning object's generated default.");
                    ++changes;
                }), PropertyEffects::None, +[](int value) { return value >= 0; });
        auto first = winrt::make_self<DependencyObject>();
        auto second = winrt::make_self<DependencyObject>();
        Require(first->GetValue(property) == 10 && first->GetValue(property) == 10 &&
            second->GetValue(property) == 20 && creations == 2,
            "A default factory must run once per instance and cache the effective default.");
        Require(!first->HasLocalValue(property) && !first->ReadLocalValue(property),
            "A generated default must not become a local value.");
        first->SetValue(property, 99);
        first->ClearValue(property);
        Require(first->GetValue(property) == 10 && creations == 2 && changes == 2,
            "ClearValue must restore the same generated default and deliver metadata notifications.");
        Rejects<winrt::hresult_illegal_method_call>([&] { (void)property.DefaultValue(); });

        int attempts{};
        auto retry = DependencyProperty<int>::RegisterWithMetadata<DependencyObject>(
            "RetryDefault", PropertyMetadata<int>(PropertyMetadata<int>::DefaultFactory(
                [&](DependencyObject&)
                {
                    if (++attempts == 1) { throw std::runtime_error("Transient factory error"); }
                    return 7;
                })));
        bool failed{};
        try { (void)first->GetValue(retry); }
        catch (std::runtime_error const&) { failed = true; }
        Require(failed && first->GetValue(retry) == 7 && attempts == 2,
            "A failing factory must clear its reentrancy guard and allow retry.");

        auto invalid = DependencyProperty<int>::RegisterWithMetadata<DependencyObject>(
            "InvalidDefault", PropertyMetadata<int>(PropertyMetadata<int>::DefaultFactory(
                [](DependencyObject&) { return -5; })), PropertyEffects::None,
            +[](int value) { return value >= 0; });
        Rejects([&] { (void)first->GetValue(invalid); });
    }

    void ErasedPropertiesShareTypedStorageAndMetadata()
    {
        int notifications{};
        auto typed = DependencyProperty<int>::Register<ObservedObject>(
            "Erased", 4, PropertyEffects::None,
            +[](int value) { return value >= 0; },
            [&](DependencyObject&, int before, int after)
            {
                Require((before == 4 && after == 9) || (before == 9 && after == 4),
                    "The typed metadata callback must observe erased writes and clears.");
                ++notifications;
            });
        auto erased = typed.Erase();
        auto object = winrt::make_self<ObservedObject>();
        Require(erased.Id() == typed.Id() && erased.Name() == "Erased" &&
            erased.ValueType() == std::type_index(typeid(int)) &&
            std::any_cast<int>(object->GetValue(erased)) == 4 &&
            winrt::unbox_value<int>(object->GetBoxedValue(erased)) == 4 &&
            !object->ReadLocalValue(erased),
            "An erased descriptor must expose the same identity, type and default.");
        int callbacks{};
        auto token = object->RegisterPropertyChangedCallback(erased,
            [&](DependencyObject&, PropertyChange const&) { ++callbacks; });
        object->SetBoxedValue(erased, winrt::box_value(9));
        Require(object->GetValue(typed) == 9 && object->ReadLocalValue(typed) == 9 &&
            notifications == 1 && callbacks == 1,
            "Erased writes must participate in typed reads and notifications.");
        Rejects([&] { object->SetValue(erased, std::any(std::string("bad"))); });
        Rejects([&] { object->SetValue(erased, std::any(-1)); });
        Rejects([&] { object->SetBoxedValue(erased, winrt::box_value(winrt::hstring(L"bad"))); });
        Rejects([&] { object->SetBoxedValue(erased, nullptr); });
        Require(object->GetValue(typed) == 9 && notifications == 1,
            "Wrong-type and invalid erased writes must preserve committed state.");
        object->ClearValue(erased);
        Require(object->GetValue(typed) == 4 && !object->ReadLocalValue(erased) &&
            notifications == 2 && callbacks == 2,
            "Erased clearing must restore typed metadata defaults.");
        object->UnregisterPropertyChangedCallback(erased, token);
        object->SetValue(typed, 9);
        Require(std::any_cast<int>(object->GetValue(erased)) == 9 && callbacks == 2,
            "Typed writes must be visible through an erased descriptor after callback revocation.");

        auto foreign = winrt::make_self<DependencyObject>();
        Rejects([&] { (void)foreign->GetValue(erased); });
        Rejects([&] { foreign->SetValue(erased, std::any(9)); });

        int factoryCalls{};
        auto generated = DependencyProperty<int>::RegisterWithMetadata<DependencyObject>(
            "ErasedLazy", PropertyMetadata<int>(PropertyMetadata<int>::DefaultFactory(
                [&](DependencyObject&) { return ++factoryCalls * 7; })));
        auto generatedHandle = generated.Erase();
        Require(std::any_cast<int>(foreign->GetValue(generatedHandle)) == 7 &&
            foreign->GetValue(generated) == 7 && factoryCalls == 1,
            "Typed and erased reads must share one lazily generated instance default.");
    }

    void NonGenericRegistrationSharesPropertyStorageAndMetadata()
    {
        int notifications{};
        auto property = RegisterBoxedProperty<ObservedObject>("NonGeneric", BoxedPropertyKind::Int32,
            { winrt::box_value(3), [&](DependencyObject&, auto const& before, auto const& after)
                {
                    auto const previous = winrt::unbox_value<int>(before);
                    auto const current = winrt::unbox_value<int>(after);
                    Require((previous == 3 && current == 8) ||
                        (previous == 8 && current == 3),
                        "Non-generic metadata must observe the typed old/new values.");
                    ++notifications;
                } });
        auto object = winrt::make_self<ObservedObject>();
        auto unset = DependencyPropertyHandle::UnsetValue();
        Require(unset && unset == DependencyPropertyHandle::UnsetValue() &&
            object->ReadLocalBoxedValue(property) == unset &&
            winrt::unbox_value<int>(object->GetBoxedValue(property)) == 3,
            "Non-generic registration must preserve defaults and a stable unset identity.");
        object->SetBoxedValue(property, winrt::box_value(8));
        Require(notifications == 1 && winrt::unbox_value<int>(object->ReadLocalBoxedValue(property)) == 8 &&
            winrt::unbox_value<int>(object->GetAnimationBaseBoxedValue(property)) == 8,
            "Non-generic writes must be visible to local and effective reads.");
        Rejects([&] { object->SetBoxedValue(property, unset); });
        Rejects([&] { object->SetBoxedValue(property, winrt::box_value(false)); });
        object->ClearValue(property);
        Require(object->ReadLocalBoxedValue(property) == unset && notifications == 2,
            "Clearing a non-generic value must restore the unset sentinel.");

        auto attached = RegisterBoxedProperty<ObservedObject>("Attached", BoxedPropertyKind::Boolean,
            { winrt::box_value(false) }, PropertyEffects::None, true);
        auto unrelated = winrt::make_self<MeasuredElement>();
        unrelated->SetBoxedValue(attached, winrt::box_value(true));
        Require(winrt::unbox_value<bool>(unrelated->GetBoxedValue(attached)),
            "An attached property must accept other DependencyObject owners.");
        Rejects([&] { (void)RegisterBoxedProperty<ObservedObject>("Invalid", BoxedPropertyKind::Double,
            { winrt::box_value(winrt::hstring(L"wrong")) }); });
    }

    void BoxedMetadataFactoriesAreLazyPerOwnerAndObservable()
    {
        using Inspectable = winrt::Windows::Foundation::IInspectable;
        int creations{};
        std::vector<std::pair<int, int>> changes;
        BoxedPropertyMetadata metadata;
        metadata.createDefault = [&](DependencyObject&) -> Inspectable
        {
            return winrt::box_value(++creations * 10);
        };
        metadata.onChanged = [&](DependencyObject&, Inspectable const& before, Inspectable const& after)
        {
            changes.emplace_back(winrt::unbox_value<int>(before), winrt::unbox_value<int>(after));
        };
        auto property = RegisterBoxedProperty<DependencyObject>(
            "BoxedGenerated", BoxedPropertyKind::Int32, metadata);
        auto published = property.GetMetadata();
        Require(static_cast<bool>(published.createDefault) && static_cast<bool>(published.onChanged) &&
            !published.defaultValue && creations == 0,
            "Metadata must expose a factory without running it at registration or inspection.");

        auto first = winrt::make_self<DependencyObject>();
        auto second = winrt::make_self<DependencyObject>();
        Require(winrt::unbox_value<int>(first->GetBoxedValue(property)) == 10 &&
            winrt::unbox_value<int>(first->GetBoxedValue(property)) == 10 &&
            winrt::unbox_value<int>(second->GetBoxedValue(property)) == 20 && creations == 2,
            "Boxed defaults must be cached independently by owning object.");
        first->SetBoxedValue(property, winrt::box_value(44));
        first->ClearValue(property);
        Require(changes == std::vector<std::pair<int, int>>{ {10, 44}, {44, 10} } && creations == 2,
            "A boxed factory must preserve the same default across local overrides and notifications.");
        published.onChanged(*second, winrt::box_value(20), winrt::box_value(21));
        Require(changes.back() == std::pair{20, 21},
            "Inspected metadata must preserve its original property changed callback.");

        auto constant = RegisterBoxedProperty<DependencyObject>(
            "BoxedConstant", BoxedPropertyKind::String,
            { winrt::box_value(winrt::hstring(L"metadata")) });
        auto constantMetadata = constant.GetMetadata();
        Require(!constantMetadata.createDefault &&
            winrt::unbox_value<winrt::hstring>(constantMetadata.defaultValue) == L"metadata",
            "Static metadata inspection must preserve the original boxed value.");

        BoxedPropertyMetadata wrongType;
        wrongType.createDefault = [](DependencyObject&) -> Inspectable
        {
            return winrt::box_value(winrt::hstring(L"wrong"));
        };
        auto invalid = RegisterBoxedProperty<DependencyObject>(
            "InvalidBoxedFactory", BoxedPropertyKind::Int32, wrongType);
        Require(creations == 2, "Factory registration must not invoke another property's default.");
        Rejects<winrt::hresult_invalid_argument>([&] { (void)first->GetBoxedValue(invalid); });
        Require(!first->ReadLocalValue(invalid), "An invalid factory must not commit a local value.");
    }

    void TypedMetadataInspectionPreservesCallbacksAndTypeValidation()
    {
        auto object = winrt::make_self<ObservedObject>();
        int observed{};
        auto property = DependencyProperty<int>::Register<ObservedObject>("MetadataCallback", 6,
            PropertyEffects::None, nullptr,
            [&](DependencyObject& owner, int before, int after)
            {
                Require(&owner == object.get(), "Metadata callback must preserve the owning object.");
                observed += after - before;
            });
        auto published = property.Erase().GetMetadata();
        Require(winrt::unbox_value<int>(published.defaultValue) == 6 &&
            static_cast<bool>(published.onChanged) && !published.createDefault,
            "Typed registration must retain its boxed default and callback in inspected metadata.");
        published.onChanged(*object, winrt::box_value(6), winrt::box_value(10));
        Rejects<winrt::hresult_invalid_argument>([&]
        {
            published.onChanged(*object, winrt::box_value(true), winrt::box_value(10));
        });
        Require(observed == 4 && object->GetValue(property) == 6,
            "Metadata inspection must not mutate the property store.");
    }

    void GeneratedDefaultKeepsDynamicRegistrationMetadataAlive()
    {
        auto object = winrt::make_self<DependencyObject>();
        std::weak_ptr<int> registration;
        {
            auto probe = std::make_shared<int>(37);
            registration = probe;
            auto property = DependencyProperty<int>::RegisterWithMetadata<DependencyObject>(
                "EphemeralDefault", PropertyMetadata<int>(PropertyMetadata<int>::DefaultFactory(
                    [probe](DependencyObject&) { return *probe; })));
            probe.reset();
            Require(object->GetValue(property) == 37,
                "The factory must produce the cached value for this object.");
        }
        Require(!registration.expired(),
            "The cached value must retain its registration identity until the owner is destroyed.");
        object = nullptr;
        Require(registration.expired(),
            "Dynamic registration metadata must be released with its last owning object.");
    }

    void EnumPropertiesSupportErasedBoxedAccess()
    {
        auto element = winrt::make_self<MeasuredElement>();
        auto visibility = UIElement::VisibilityProperty.Erase();
        Require(winrt::unbox_value<std::int32_t>(element->GetBoxedValue(visibility)) ==
            static_cast<std::int32_t>(Visibility::Visible),
            "Enum dependency properties must expose their underlying value.");
        element->SetBoxedValue(visibility, winrt::box_value(static_cast<std::int32_t>(Visibility::Collapsed)));
        Require(element->VisibilityState() == Visibility::Collapsed,
            "An erased enum write must update typed state.");
        Rejects([&] { element->SetBoxedValue(visibility, winrt::box_value(100)); });
        Require(element->VisibilityState() == Visibility::Collapsed,
            "An invalid enum value must not modify the property.");
    }

    void BoxedStringAndInt64PropertiesPreserveValueTypes()
    {
        auto object = winrt::make_self<ObservedObject>();
        auto textProperty = RegisterBoxedProperty<ObservedObject>("Label", BoxedPropertyKind::String,
            { winrt::box_value(winrt::hstring(L"initial")) });
        object->SetBoxedValue(textProperty, winrt::box_value(winrt::hstring(L"updated")));
        Require(winrt::unbox_value<winrt::hstring>(object->GetBoxedValue(textProperty)) == L"updated",
            "String values must round-trip through boxed property access.");
        Rejects([&] { object->SetBoxedValue(textProperty, winrt::box_value(123)); });

        auto countProperty = RegisterBoxedProperty<ObservedObject>("Count", BoxedPropertyKind::Int64,
            { winrt::box_value(std::int64_t{ 1LL << 40 }) });
        Require(winrt::unbox_value<std::int64_t>(object->GetBoxedValue(countProperty)) == (1LL << 40),
            "64-bit defaults must retain precision.");
        object->SetBoxedValue(countProperty, winrt::box_value(std::int64_t{ 1LL << 44 }));
        Require(winrt::unbox_value<std::int64_t>(object->GetBoxedValue(countProperty)) == (1LL << 44),
            "64-bit local values must retain precision.");
    }

    void PropertyValuePrecedencePreservesLayersAndOnlyNotifiesEffectiveChanges()
    {
        auto object = winrt::make_self<ObservedObject>();
        auto const& property = ObservedObject::ValueProperty;
        auto erased = property.Erase();
        std::vector<std::pair<int, int>> transitions;
        auto token = object->RegisterPropertyChangedCallback(erased,
            [&](DependencyObject&, PropertyChange const& change)
            {
                transitions.emplace_back(std::any_cast<int>(change.oldValue),
                    std::any_cast<int>(change.newValue));
            });
        Require(object->GetValue(property) == 0 && object->GetAnimationBaseValue(property) == 0,
            "Metadata default must be the lowest-priority value.");
        object->SetStyleValue(property, 2);
        Require(object->GetValue(property) == 2 && !object->ReadLocalValue(property),
            "Style values must override defaults without setting a local value.");
        object->SetValue(property, 3);
        object->SetAnimationValue(property, 4);
        Require(object->GetValue(property) == 4 && object->GetAnimationBaseValue(property) == 3 &&
            winrt::unbox_value<int>(object->GetAnimationBaseBoxedValue(erased)) == 3,
            "An active animation must override local values but not their animation-base value.");

        object->SetValue(property, 5);
        object->SetStyleValue(erased, std::any(6));
        Require(object->GetValue(property) == 4 && object->GetAnimationBaseValue(property) == 5 &&
            object->ReadLocalValue(property) == 5,
            "Shadowed layer changes must remain stored without changing the effective value.");
        Rejects([&] { object->SetAnimationValue(erased, std::any(-1)); });
        Rejects([&] { object->SetStyleValue(erased, std::any(std::string("bad"))); });
        Require(object->GetValue(property) == 4,
            "Invalid shadowed layer values must not mutate committed state.");

        object->ClearAnimationValue(property);
        Require(object->GetValue(property) == 5 && object->GetAnimationBaseValue(property) == 5,
            "Stopping an animation must reveal the stored local value.");
        object->ClearValue(property);
        Require(object->GetValue(property) == 6 && !object->ReadLocalValue(property),
            "Clearing a local value must reveal its style value.");
        object->ClearStyleValue(property);
        object->ClearStyleValue(property);
        Require(object->GetValue(property) == 0 &&
            transitions == std::vector<std::pair<int, int>>{
                { 0, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 }, { 5, 6 }, { 6, 0 } },
            "Callbacks must record only changes of the committed effective value.");
        object->UnregisterPropertyChangedCallback(erased, token);

        auto element = winrt::make_self<MeasuredElement>();
        auto const version = element->RenderVersion();
        element->SetAnimationValue(UIElement::OpacityProperty, 0.7);
        Require(element->Opacity() == 0.7 && element->RenderVersion() > version,
            "An animated render property must propagate invalidation through the property engine.");
        auto const shadowedVersion = element->RenderVersion();
        element->Opacity(0.3);
        Require(element->RenderVersion() == shadowedVersion &&
            element->GetAnimationBaseValue(UIElement::OpacityProperty) == 0.3,
            "Changing a shadowed property must not redraw an unchanged effective value.");
        element->ClearAnimationValue(UIElement::OpacityProperty);
        Require(element->Opacity() == 0.3 && element->RenderVersion() > shadowedVersion,
            "Ending an animation must invalidate when the visible value changes.");
    }

    void LayoutRoundingUsesPhysicalPixelBoundaries()
    {
        auto element = winrt::make_self<MeasuredElement>();
        Require(element->UseLayoutRounding(), "Layout rounding must be enabled by default.");
        Rect const fractional{ 0.6, 1.4, 10.6, 8.2 };
        element->Arrange(fractional);
        Require(element->LayoutSlot() == Rect{ 1, 1, 11, 8 },
            "Default layout rounding must snap arranged coordinates and dimensions to whole pixels.");
        element->UseLayoutRounding(false);
        Require(!element->IsArrangeValid() && element->IsMeasureValid(),
            "Changing layout rounding must invalidate arrange without invalidating measure.");
        element->Arrange(fractional);
        Require(element->LayoutSlot() == fractional,
            "Disabling layout rounding must preserve fractional arranged values.");
        element->UseLayoutRounding(true);
        element->Arrange(fractional);
        Require(element->LayoutSlot() == Rect{ 1, 1, 11, 8 },
            "Reenabling layout rounding must snap the next arrangement.");
    }

    void ContentRootPropagatesRasterizationScaleToChildren()
    {
        auto panel = winrt::make_self<ContentControl>();
        auto child = winrt::make_self<MeasuredElement>();
        panel->Content(child.get());
        std::deque<std::function<void()>> work;
        auto enqueue = [&](std::function<void()> callback)
        {
            work.push_back(std::move(callback));
            return true;
        };
        winrt::com_ptr<UIElement> hosted;
        hosted.copy_from(static_cast<UIElement*>(panel.get()));
        auto root = ContentRoot::Create(hosted, { 100.4, 60.4 }, enqueue, [](UIElement&) {});
        Rejects([&] { root->SetRasterizationScale(0); });
        Rejects([&] { root->SetRasterizationScale(std::numeric_limits<double>::denorm_min()); });
        Rejects([&] { root->SetRasterizationScale(std::numeric_limits<double>::infinity()); });
        Require(root->RasterizationScale() == 1.0,
            "Invalid rasterization scales must preserve the existing scale.");
        root->SetRasterizationScale(1.5);
        Require(work.size() == 1 && root->RasterizationScale() == 1.5,
            "Changing DPI before a frame must retain a single queued layout pass.");
        auto run = [&]
        {
            auto callback = std::move(work.front());
            work.pop_front();
            callback();
        };
        run();
        auto const expectedWidth = std::round(100.4 * 1.5) / 1.5;
        auto const expectedHeight = std::round(60.4 * 1.5) / 1.5;
        Require(panel->LayoutSlot().width == expectedWidth &&
            child->LayoutSlot().width == expectedWidth &&
            child->LayoutSlot().height == expectedHeight && root->FrameCount() == 1,
            "Root scale must flow to descendant arrangements and render in one frame.");
        root->SetRasterizationScale(1.0);
        run();
        Require(panel->LayoutSlot().width == 100 && child->LayoutSlot().width == 100 &&
            child->LayoutSlot().height == 60 && root->FrameCount() == 2,
            "DPI changes must invalidate and reround previously arranged descendants.");
        root->SetRasterizationScale(1.5);
        root->Shutdown();
        Require(child->LayoutSlot().width == 100 && !child->IsArrangeValid(),
            "Root shutdown must reset layout scale on retained detached elements.");
        Rejects<winrt::hresult_illegal_method_call>([&] { root->SetRasterizationScale(2); });
    }

    void LayoutRoundingInheritsAcrossVisualTreeAndRespectsOverrides()
    {
        auto panel = winrt::make_self<ContentControl>();
        auto child = winrt::make_self<MeasuredElement>();
        panel->Content(child.get());
        Rect const fractional{ 0.6, 1.4, 10.6, 8.2 };

        child->Arrange(fractional);
        Require(child->LayoutSlot() == Rect{ 1, 1, 11, 8 },
            "Default layout rounding must apply to an attached child.");
        panel->UseLayoutRounding(false);
        Require(!child->UseLayoutRounding() && !child->IsArrangeValid(),
            "Ancestor layout rounding changes must invalidate descendant arrangement.");
        child->Arrange(fractional);
        Require(child->LayoutSlot() == fractional,
            "Child layout must inherit disabled rounding from its parent.");

        child->SetStyleValue(UIElement::UseLayoutRoundingProperty, true);
        Require(child->UseLayoutRounding(), "Child style values must override inherited rounding.");
        child->Arrange(fractional);
        Require(child->LayoutSlot() == Rect{ 1, 1, 11, 8 },
            "The explicit child style must restore pixel rounding.");
        child->ClearStyleValue(UIElement::UseLayoutRoundingProperty);
        Require(!child->UseLayoutRounding(), "Clearing the child style must restore inheritance.");
        child->Arrange(fractional);
        Require(child->LayoutSlot() == fractional, "Inherited rounding must apply after clearing a style.");

        panel->Content(nullptr);
        Require(child->UseLayoutRounding() && !child->IsArrangeValid(),
            "Detaching from an ancestor must restore the default and invalidate arrangement.");
        child->Arrange(fractional);
        Require(child->LayoutSlot() == Rect{ 1, 1, 11, 8 },
            "Detached elements must arrange using their own default rounding.");
    }

    void HitTestingFollowsCompositionOrderAndVisibility()
    {
        auto root = winrt::make_self<HitTestOverlay>();
        auto back = winrt::make_self<FrameworkElement>();
        auto front = winrt::make_self<FrameworkElement>();
        root->Add(back.get(), front.get());
        auto layout = [&]
        {
            root->Measure({ 80, 60 });
            root->Arrange({ 0, 0, 80, 60 });
        };
        Require(!root->HitTest({ 10, 10 }), "Hit testing must require settled layout.");
        layout();
        Require(root->HitTest({ 10, 10 }).get() == front.get(),
            "The last painted visual must be hit first.");

        front->ClipBounds(Rect{ 20, 20, 10, 10 });
        Require(root->HitTest({ 10, 10 }).get() == back.get() &&
            root->HitTest({ 25, 25 }).get() == front.get(),
            "Hit testing must honor explicit composition clipping of the top visual.");
        front->ClipBounds(std::nullopt);
        front->IsHitTestVisible(false);
        Require(root->HitTest({ 10, 10 }).get() == back.get(),
            "Disabling hit testing must expose visuals beneath the disabled sibling.");
        front->IsHitTestVisible(true);
        front->VisibilityState(Visibility::Collapsed);
        Require(!root->HitTest({ 10, 10 }), "Dirty layout must not be used for hit testing.");
        layout();
        Require(root->HitTest({ 10, 10 }).get() == back.get(),
            "Collapsed visuals must not intercept hit testing.");
        front->VisibilityState(Visibility::Visible);
        layout();
        root->IsHitTestVisible(false);
        Require(!root->HitTest({ 10, 10 }),
            "A non-hittable ancestor must also disable hit testing of its descendants.");
        root->IsHitTestVisible(true);
        root->IsEnabled(false);
        Require(!root->HitTest({ 10, 10 }), "Disabled trees must not accept pointer input.");
        root->IsEnabled(true);
        Require(!root->HitTest({ -1, -1 }) && !root->HitTest({ 80, 60 }),
            "Hit testing must reject coordinates outside the realized geometry.");
        Rejects([&] { (void)root->HitTest({ std::numeric_limits<double>::quiet_NaN(), 0 }); });
    }

    void FlowDirectionInheritsAndMirrorsHitTesting()
    {
        auto root = winrt::make_self<StackPanel>();
        root->Orientation(Orientation::Horizontal);
        auto first = winrt::make_self<FrameworkElement>();
        auto second = winrt::make_self<FrameworkElement>();
        first->Width(30);
        first->Height(20);
        second->Width(30);
        second->Height(20);
        root->Append(first.get());
        root->Append(second.get());
        auto layout = [&]
        {
            root->Measure({ 120, 40 });
            root->Arrange({ 0, 0, 120, 40 });
        };
        layout();
        Require(root->HitTest({ 10, 10 }).get() == first.get() &&
            root->HitTest({ 40, 10 }).get() == second.get(),
            "LTR hit testing must use unmirrored child slots.");

        auto const previousVersion = root->RenderVersion();
        root->FlowDirectionState(FlowDirection::RightToLeft);
        Require(root->IsRightToLeft() && first->IsRightToLeft() && second->IsRightToLeft() &&
            root->RenderVersion() > previousVersion &&
            !first->IsArrangeValid() && !second->IsArrangeValid(),
            "RTL must inherit and invalidate descendant arrangements and rendering.");
        layout();
        Require(root->HitTest({ 110, 10 }).get() == first.get() &&
            root->HitTest({ 80, 10 }).get() == second.get() &&
            root->HitTest({ 10, 10 }).get() == root.get(),
            "RTL root coordinates must reflect the Composition mirror transform.");

        second->FlowDirectionState(FlowDirection::LeftToRight);
        Require(!second->IsRightToLeft() && first->IsRightToLeft(),
            "An explicitly LTR child must override inherited RTL without changing siblings.");
        Rejects<winrt::hresult_invalid_argument>([&]
        {
            root->FlowDirectionState(static_cast<FlowDirection>(20));
        });
        root->ClearValue(FrameworkElement::FlowDirectionProperty);
        layout();
        Require(!root->IsRightToLeft() && !first->IsRightToLeft() &&
            root->HitTest({ 10, 10 }).get() == first.get(),
            "Clearing FlowDirection must restore LTR and its hit-test transform.");
    }

    void LifecycleEventSubscriptionsRespectMutationAndDeferredDelivery()
    {
        auto element = winrt::make_self<MeasuredElement>();
        std::vector<std::string> events;
        std::int64_t cancelledLoading{};
        auto firstLoading = element->AddLoadingHandler([&](UIElement& source)
        {
            Require(&source == element.get(), "Loading must receive the original element.");
            events.emplace_back("Loading");
            element->RemoveLoadingHandler(cancelledLoading);
            (void)element->AddLoadingHandler([&](UIElement&) { events.emplace_back("late Loading"); });
        });
        cancelledLoading = element->AddLoadingHandler([&](UIElement&)
        {
            events.emplace_back("removed Loading");
        });
        std::int64_t removedLoaded{};
        auto firstLoaded = element->AddLoadedHandler([&](UIElement&)
        {
            events.emplace_back("Loaded");
            element->RemoveLoadedHandler(removedLoaded);
        });
        removedLoaded = element->AddLoadedHandler([&](UIElement&)
        {
            events.emplace_back("removed Loaded");
        });
        auto unloaded = element->AddUnloadedHandler([&](UIElement& source)
        {
            Require(&source == element.get(), "Unloaded must retain the detached element.");
            events.emplace_back("Unloaded");
        });
        Rejects([&] { (void)element->AddLoadedHandler({}); });

        std::deque<std::function<void()>> work;
        auto enqueue = [&](std::function<void()> callback)
        {
            work.push_back(std::move(callback));
            return true;
        };
        auto flush = [&]
        {
            while (!work.empty())
            {
                auto callback = std::move(work.front());
                work.pop_front();
                callback();
            }
        };
        winrt::com_ptr<UIElement> hosted;
        hosted.copy_from(element.get());
        auto root = ContentRoot::Create(hosted, { 90, 70 }, enqueue, [](UIElement&) {});
        flush();
        Require(events == std::vector<std::string>{ "Loading", "Loaded" },
            "Loading and Loaded must fire once in order; removed and late handlers must not run.");
        root->Resize({ 100, 80 });
        flush();
        Require(events.size() == 2, "Relayout must not repeat lifecycle events.");
        root->Shutdown();
        Require(events.size() == 2, "Unloaded must be deferred until the dispatcher callback runs.");
        flush();
        Require(events == std::vector<std::string>{ "Loading", "Loaded", "Unloaded" },
            "Shutdown must deliver one deferred Unloaded event.");

        root = ContentRoot::Create(hosted, { 90, 70 }, enqueue, [](UIElement&) {});
        flush();
        Require(events.back() == "Loaded" && events.size() == 4,
            "Reattachment must raise Loaded again without repeating Loading.");
        element->RemoveUnloadedHandler(unloaded);
        element->RemoveLoadingHandler(firstLoading);
        element->RemoveLoadedHandler(firstLoaded);
        root->Shutdown();
        flush();
        Require(events.size() == 4, "Removed Unloaded handlers must not receive queued events.");
    }

    void SynchronousUpdateLayoutConsumesQueuedFrameWithoutReentering()
    {
        auto host = winrt::make_self<ContentControl>();
        auto child = winrt::make_self<MeasuredElement>();
        host->Content(child.get());
        winrt::com_ptr<UIElement> visual;
        visual.copy_from(host.get());
        std::deque<std::function<void()>> work;
        int rendered{};
        int reentrantBlocked{};
        auto root = ContentRoot::Create(visual, { 100, 60 },
            [&](std::function<void()> callback)
            {
                work.push_back(std::move(callback));
                return true;
            },
            [&](UIElement&)
            {
                ++rendered;
                Rejects<winrt::hresult_illegal_method_call>([&] { child->UpdateLayout(); });
                ++reentrantBlocked;
            });

        Require(work.size() == 1 && root->FrameCount() == 0,
            "The initial frame must remain pending before synchronous layout.");
        child->UpdateLayout();
        Require(root->FrameCount() == 1 && rendered == 1 && reentrantBlocked == 1 &&
            child->IsLoaded() && child->IsArrangeValid(),
            "UpdateLayout on a descendant must synchronously settle the root once.");
        auto posted = std::move(work.front());
        work.pop_front();
        posted();
        Require(root->FrameCount() == 1 && rendered == 1,
            "A previously enqueued dispatcher callback must not run the consumed frame again.");
        child->Width(40);
        Require(work.size() == 1 && !child->IsMeasureValid(),
            "Invalidating after synchronous layout must enqueue a fresh frame.");
        root->UpdateLayout();
        Require(root->FrameCount() == 2 && rendered == 2 && child->IsMeasureValid(),
            "Synchronous root layout must settle property invalidations.");
        root->Shutdown();
        child->UpdateLayout();
        Rejects<winrt::hresult_illegal_method_call>([&] { root->UpdateLayout(); });
    }

    void RejectedDispatcherRollsBackRootAttachment()
    {
        auto element = winrt::make_self<MeasuredElement>();
        winrt::com_ptr<UIElement> hosted;
        hosted.copy_from(element.get());
        Rejects<winrt::hresult_illegal_method_call>([&]
        {
            (void)ContentRoot::Create(hosted, { 80, 60 },
                [](std::function<void()>) { return false; }, [](UIElement&) {});
        });
        Require(!element->IsConnectedToRoot() && !element->IsLoaded(),
            "A rejected dispatcher enqueue must disconnect the failed root.");

        std::deque<std::function<void()>> work;
        auto root = ContentRoot::Create(hosted, { 80, 60 },
            [&](std::function<void()> callback)
            {
                work.push_back(std::move(callback));
                return true;
            }, [](UIElement&) {});
        auto frame = std::move(work.front());
        work.pop_front();
        frame();
        Require(root->FrameCount() == 1 && element->IsLoaded(),
            "A failed root creation must leave its element available for a new dispatcher.");
        root->Shutdown();
    }

    void ClipBoundsInvalidatesRenderingWithoutChangingLayout()
    {
        auto element = winrt::make_self<MeasuredElement>();
        element->Measure({ 80, 60 });
        element->Arrange({ 0, 0, 80, 60 });
        auto const version = element->RenderVersion();
        element->ClipBounds(Rect{ 1, 2, 30, 20 });
        Require(element->ClipBounds() == Rect{ 1, 2, 30, 20 } &&
            element->RenderVersion() > version && element->IsMeasureValid() && element->IsArrangeValid(),
            "Clip updates must affect rendering but preserve settled layout.");
        Rejects([&] { element->ClipBounds(Rect{ 0, 0, -1, 2 }); });
        Require(element->ClipBounds() == Rect{ 1, 2, 30, 20 },
            "Invalid clipping bounds must not replace a valid clip.");
        element->ClipBounds(std::nullopt);
        Require(!element->ClipBounds(), "Clips must be removable.");
    }

    void SizeChangedHandlersObserveCommittedLayoutAndSafeRevocation()
    {
        auto element = winrt::make_self<MeasuredElement>();
        int delivered{};
        int revoked{};
        std::int64_t revokedToken{};
        auto const token = element->AddSizeChangedHandler(
            [&](FrameworkElement& sender, Size previous, Size current)
            {
                Require(&sender == static_cast<FrameworkElement*>(element.get()) &&
                    sender.ActualSize() == current && previous != current,
                    "SizeChanged must observe committed geometry.");
                ++delivered;
                element->RemoveSizeChangedHandler(revokedToken);
            });
        revokedToken = element->AddSizeChangedHandler(
            [&](FrameworkElement&, Size, Size) { ++revoked; });
        element->Measure({ 100, 80 });
        element->Arrange({ 0, 0, 60, 40 });
        element->Arrange({ 0, 0, 60, 40 });
        element->Arrange({ 0, 0, 80, 50 });
        Require(delivered == 2 && revoked == 0,
            "SizeChanged must only fire for changed sizes, honoring removal during delivery.");
        element->RemoveSizeChangedHandler(token);
        element->Arrange({ 0, 0, 90, 60 });
        Require(delivered == 2, "Removed SizeChanged handlers must remain inactive.");
        Rejects([&] { (void)element->AddSizeChangedHandler({}); });
    }

    PointerInput Pointer(PointerAction action, bool over = true, std::uint32_t id = 1)
    {
        return { action, id, over };
    }

    void PropertyStorageAndValidation()
    {
        auto object = winrt::make_self<ObservedObject>();
        auto const& property = ObservedObject::ValueProperty;
        Require(object->GetValue(property) == 0 && !object->HasLocalValue(property), "Default must not occupy local storage.");
        object->SetValue(property, 3);
        object->SetValue(property, 3);
        Require(object->notifications == 1, "Equal values must not generate duplicate notifications.");
        Rejects([&] { object->SetValue(property, -1); });
        Require(object->GetValue(property) == 3, "Invalid writes must preserve storage.");
        object->ClearValue(property);
        Require(object->GetValue(property) == 0 && !object->HasLocalValue(property), "Clear must restore the default.");
        auto foreign = winrt::make_self<DependencyObject>();
        Rejects([&] { foreign->SetValue(property, 4); });
        Rejects([&] { (void)foreign->GetValue(property); });
    }

    void AttachedPropertyAcceptsAnyDependencyObject()
    {
        auto attached = DependencyProperty<int>::RegisterAttached<ObservedObject>("Row", 0,
            PropertyEffects::None, +[](int row) { return row >= 0; });
        auto element = winrt::make_self<FrameworkElement>();
        auto plain = winrt::make_self<DependencyObject>();
        Require(element->GetValue(attached) == 0 && plain->GetValue(attached) == 0,
            "Attached properties must expose defaults on unrelated DependencyObjects.");
        plain->SetValue(attached, 3);
        element->SetValue(attached, 2);
        Require(plain->GetValue(attached) == 3 && element->GetValue(attached) == 2,
            "Attached values must be stored on each target object independently.");
        Rejects([&] { plain->SetValue(attached, -1); });
        plain->ClearValue(attached);
        Require(!plain->HasLocalValue(attached) && plain->GetValue(attached) == 0,
            "Clearing an attached property must restore its metadata default.");
    }

    void PropertyMetadataOutlivesRegistrationHandle()
    {
        auto object = winrt::make_self<DependencyObject>();
        {
            auto temporary = DependencyProperty<int>::Register<DependencyObject>("Temporary", 5);
            object->SetValue(temporary, 7);
        }
        auto other = DependencyProperty<int>::Register<DependencyObject>("Temporary", 11);
        Require(object->GetValue(other) == 11, "Distinct registrations must not alias a retired metadata address.");
    }

    void PropertyChangedCallbacksArePropertyScopedAndRevocable()
    {
        auto object = winrt::make_self<ObservedObject>();
        auto other = DependencyProperty<int>::Register<ObservedObject>("Other", 0);
        std::vector<int> observed;
        std::vector<std::pair<int, int>> changes;
        auto const token = object->RegisterPropertyChangedCallback(ObservedObject::ValueProperty,
            [&](DependencyObject& sender, PropertyChange const& change)
            {
                Require(&sender == object.get() && change.property == ObservedObject::ValueProperty.Id(),
                    "The callback must identify the owner and registered property.");
                observed.push_back(object->GetValue(ObservedObject::ValueProperty));
                changes.emplace_back(std::any_cast<int>(change.oldValue), std::any_cast<int>(change.newValue));
            });
        Require(!object->ReadLocalValue(ObservedObject::ValueProperty).has_value(),
            "An unset local value must be distinguishable from the default.");
        Require(object->GetAnimationBaseValue(ObservedObject::ValueProperty) == 0,
            "Without animations, the animation base value must equal the effective value.");
        object->SetValue(other, 1);
        object->SetValue(ObservedObject::ValueProperty, 5);
        object->SetValue(ObservedObject::ValueProperty, 5);
        Require(object->ReadLocalValue(ObservedObject::ValueProperty) == 5,
            "Reading a local value must return the stored value.");
        object->ClearValue(ObservedObject::ValueProperty);
        Require(!object->ReadLocalValue(ObservedObject::ValueProperty).has_value(),
            "Clearing a property must remove the local entry.");
        Require(observed == std::vector<int>{ 5, 0 },
            "Only effective changes for the registered property should notify.");
        Require(changes == (std::vector<std::pair<int, int>>{ { 0, 5 }, { 5, 0 } }),
            "Property change records must retain the old and new values.");
        object->UnregisterPropertyChangedCallback(ObservedObject::ValueProperty, token);
        object->SetValue(ObservedObject::ValueProperty, 7);
        Require(observed.size() == 2, "Unregistered callbacks must stay inactive.");
        Rejects([&] { (void)object->RegisterPropertyChangedCallback(ObservedObject::ValueProperty, {}); });
        Rejects([&] { (void)object->RegisterPropertyChangedCallback(DependencyProperty<int>::Register<FrameworkElement>("Foreign", 0),
            [](DependencyObject&, PropertyChange const&) {}); });
    }

    void PropertyChangedCallbacksSupportReentrancyAndMutation()
    {
        auto object = winrt::make_self<ObservedObject>();
        auto const& property = ObservedObject::ValueProperty;
        std::vector<int> calls;
        std::int64_t secondToken{};
        (void)object->RegisterPropertyChangedCallback(property,
            [&](DependencyObject&, PropertyChange const&)
            {
                calls.push_back(1);
                object->UnregisterPropertyChangedCallback(property, secondToken);
                if (object->GetValue(property) == 1)
                {
                    object->SetValue(property, 2);
                }
            });
        secondToken = object->RegisterPropertyChangedCallback(property,
            [&](DependencyObject&, PropertyChange const&) { calls.push_back(2); });
        object->SetValue(property, 1);
        Require(object->GetValue(property) == 2 && calls == std::vector<int>{ 1, 1 },
            "Reentrant changes should commit and revoked handlers must not run.");

        std::int64_t lateToken{};
        auto const firstToken = object->RegisterPropertyChangedCallback(property,
            [&](DependencyObject&, PropertyChange const&)
            {
                calls.push_back(3);
                if (!lateToken)
                {
                    lateToken = object->RegisterPropertyChangedCallback(property,
                        [&](DependencyObject&, PropertyChange const&) { calls.push_back(4); });
                }
            });
        object->SetValue(property, 3);
        Require(calls == std::vector<int>{ 1, 1, 1, 3 },
            "Handlers added during a notification must not see the current change.");
        object->SetValue(property, 4);
        Require(calls == std::vector<int>{ 1, 1, 1, 3, 1, 3, 4 },
            "Handlers added during an earlier change must run on subsequent changes.");
        object->UnregisterPropertyChangedCallback(property, firstToken);
        object->UnregisterPropertyChangedCallback(property, lateToken);
    }

    void PropertyMetadataCallbacksObserveCommittedOldAndNewValues()
    {
        auto object = winrt::make_self<ObservedObject>();
        std::vector<std::pair<int, int>> metadataChanges;
        std::optional<DependencyProperty<int>> property;
        property.emplace(DependencyProperty<int>::Register<ObservedObject>("MetadataValue", 0,
            PropertyEffects::None, nullptr,
            [&](DependencyObject& owner, int previous, int current)
            {
                Require(&owner == object.get() && owner.GetValue(*property) == current,
                    "Metadata callbacks must see the committed value on the owning object.");
                metadataChanges.emplace_back(previous, current);
            }));
        object->SetValue(*property, 1);
        object->SetValue(*property, 1);
        object->ClearValue(*property);
        Require(metadataChanges == (std::vector<std::pair<int, int>>{ { 0, 1 }, { 1, 0 } }),
            "Metadata callbacks must receive old/new values for effective changes only.");
    }

    void PropertyNotificationsSeeCommittedState()
    {
        auto object = winrt::make_self<ObservedObject>();
        object->reenter = true;
        object->SetValue(ObservedObject::ValueProperty, 1);
        Require(object->GetValue(ObservedObject::ValueProperty) == 2 && object->notifications == 2,
            "Nested writes must see committed storage and remain authoritative.");
        object->throwInNotification = true;
        Rejects<std::runtime_error>([&] { object->SetValue(ObservedObject::ValueProperty, 4); });
        Require(object->GetValue(ObservedObject::ValueProperty) == 4, "A notification exception must not roll back committed storage.");
    }

    void SingleParentAndCycleChecksAreAtomic()
    {
        auto first = winrt::make_self<ContentControl>();
        auto second = winrt::make_self<ContentControl>();
        auto child = winrt::make_self<FrameworkElement>();
        first->Content(child.get());
        Require(child->Parent().get() == first.get(), "The visual child must point to its real owner.");
        Rejects([&] { second->Content(child.get()); });
        Require(first->Content().get() == child.get() && !second->Content(), "Failed association must preserve both trees.");
        first->Content(child.get());
        Rejects([&] { first->Content(first.get()); });
        Require(first->Content().get() == child.get(), "Self-parenting rejection must preserve existing content.");
        first->Content(second.get());
        Rejects([&] { second->Content(first.get()); });
        Require(!first->Parent() && second->Parent().get() == first.get(), "Ancestor cycles must be rejected before mutation.");
    }

    void NativeTreeOwnsChildrenWithoutOwningItsParent()
    {
        auto parent = winrt::make_self<ContentControl>();
        auto child = winrt::make_self<FrameworkElement>();
        auto weakChild = child->DependencyObject::get_weak();
        parent->Content(child.get());
        child = nullptr;
        Require(static_cast<bool>(weakChild.get()), "A parent must own its child.");
        parent->Content(nullptr);
        Require(!weakChild.get(), "Removing the only owning edge must release the child.");
        child = winrt::make_self<FrameworkElement>();
        parent->Content(child.get());
        auto weakParent = parent->DependencyObject::get_weak();
        parent = nullptr;
        Require(!weakParent.get() && !child->Parent(), "A surviving child must not retain its parent.");
    }

    void ContentParticipatesInLayoutAndInvalidation()
    {
        auto parent = winrt::make_self<Button>();
        auto child = winrt::make_self<MeasuredElement>();
        parent->Content(child.get());
        parent->Measure({ 100, 50 });
        Require(parent->DesiredSize() == Size{ 20, 10 }, "Content size must determine desired size.");
        parent->Arrange({ 3, 4, 100, 50 });
        Require(child->LayoutSlot() == Rect{ 0, 0, 100, 50 }, "Content must be arranged relative to its owner.");
        parent->Measure({ 100, 50 });
        parent->Arrange({ 3, 4, 100, 50 });
        Require(child->measures == 1 && child->arranges == 1, "Clean layout must reuse cached results.");
        child->Width(40);
        Require(!parent->IsMeasureValid() && !parent->IsArrangeValid(), "Child measure invalidation must propagate to ancestors.");
        parent->Measure({ 100, 50 });
        Require(parent->DesiredSize() == Size{ 40, 10 }, "Typed setters and layout must use the same property storage.");
        child->ClearValue(FrameworkElement::WidthProperty);
        Require(std::isnan(child->Width()), "Clearing auto width must restore NaN.");
        Rejects([&] { child->Height(-1); });
        Rejects([&] { child->Width(std::numeric_limits<double>::infinity()); });
    }

    void LayoutRetainsInvalidationAndRecoversAfterExceptions()
    {
        auto element = winrt::make_self<MeasuredElement>();
        element->invalidateDuringMeasure = true;
        element->Measure({ 100, 50 });
        Require(!element->IsMeasureValid(), "Invalidation from an override must not be overwritten at pass completion.");
        element->invalidateDuringMeasure = false;
        element->throwDuringMeasure = true;
        Rejects<std::runtime_error>([&] { element->Measure({ 100, 50 }); });
        Require(!element->IsMeasureValid(), "A failed measure must remain dirty.");
        element->throwDuringMeasure = false;
        element->Measure({ 100, 50 });
        Require(element->IsMeasureValid(), "A subsequent measure must recover after failure.");
        Rejects([&] { element->Measure({ -1, 50 }); });
        Rejects([&] { element->Arrange({ 0, 0, std::numeric_limits<double>::infinity(), 50 }); });
    }

    void PointerReleaseUsesOneObjectAndOneClick()
    {
        auto button = winrt::make_self<Button>();
        int clicks{};
        auto const identity = button.as<winrt::Windows::Foundation::IInspectable>();
        (void)button->AddClick([&](ButtonBase& sender)
        {
            Require(static_cast<winrt::Windows::Foundation::IInspectable>(sender) == identity, "Click must originate from the canonical object.");
            Require(!sender.ActivePointer() && !sender.IsPressed(), "Release state must commit before Click.");
            ++clicks;
        });
        Require(button->ProcessPointer(Pointer(PointerAction::Pressed)), "Primary pointer press must be handled.");
        Require(button->IsPressed() && button->VisualState() == ButtonVisualState::Pressed && clicks == 0,
            "Release mode must wait for pointer release.");
        button->ProcessPointer(Pointer(PointerAction::Released));
        Require(clicks == 1 && button->VisualState() == ButtonVisualState::PointerOver, "Release must click exactly once.");
        Require(!button->ProcessPointer(Pointer(PointerAction::Released)) && clicks == 1, "Unpaired releases must not click.");
    }

    void ArrangeAndRenderInvalidationReachAncestors()
    {
        auto parent = winrt::make_self<ContentControl>();
        auto child = winrt::make_self<MeasuredElement>();
        parent->Content(child.get());
        parent->Measure({ 100, 50 });
        parent->Arrange({ 0, 0, 100, 50 });
        auto const renderVersion = parent->RenderVersion();
        child->InvalidateArrange();
        Require(parent->IsMeasureValid() && !parent->IsArrangeValid(), "Arrange invalidation must reach the parent without forcing measure.");
        parent->Arrange({ 0, 0, 100, 50 });
        Require(child->arranges == 2 && parent->IsArrangeValid(), "A clean parent's cached slot must not skip dirty child arrange.");
        Require(parent->RenderVersion() != renderVersion, "Child updates must advance the ancestor render version.");
        parent->Content(child.get());
        Require(parent->IsMeasureValid(), "Assigning the same child must be a no-op.");
    }

    void LayoutRejectsRecursiveAndInvalidOverrides()
    {
        auto element = winrt::make_self<MeasuredElement>();
        element->recurseDuringMeasure = true;
        Rejects<winrt::hresult_illegal_method_call>([&] { element->Measure({ 100, 50 }); });
        element->recurseDuringMeasure = false;
        element->invalidResult = true;
        Rejects([&] { element->Measure({ 100, 50 }); });
        element->invalidResult = false;
        element->Measure({ 100, 50 });
        Require(element->IsMeasureValid(), "Failed recursive/invalid overrides must not poison subsequent layout.");
    }

    void PropertyChangePinsItsNativeOwner()
    {
        auto object = winrt::make_self<ObservedObject>();
        auto weak = object->DependencyObject::get_weak();
        bool completed{};
        object->changed = [&]
        {
            object = nullptr;
            Require(static_cast<bool>(weak.get()), "A property notification must retain its native owner.");
            completed = true;
        };
        auto* raw = object.get();
        raw->SetValue(ObservedObject::ValueProperty, 3);
        Require(completed && !weak.get(), "The owner must be released after the property operation finishes.");
    }

    struct ReparentingElement : winrt::implements<ReparentingElement, FrameworkElement>
    {
        std::function<void()> parentChanged;
        void OnVisualParentChanged() override
        {
            auto callback = std::exchange(parentChanged, {});
            if (callback) { callback(); }
        }
    };

    void ReentrantContentChangesRetainTheNewestTree()
    {
        auto parent = winrt::make_self<ContentControl>();
        auto first = winrt::make_self<ReparentingElement>();
        auto second = winrt::make_self<FrameworkElement>();
        first->parentChanged = [&] { parent->Content(second.get()); };
        parent->Content(first.get());
        Require(parent->Content().get() == second.get() && second->Parent().get() == parent.get() && !first->Parent(),
            "A parent-change callback must not let the outer mutation overwrite a newer tree.");
    }

    std::function<void()> propertyValidation;

    void ValidationRetainsTheOwnerAndSeesNestedWrites()
    {
        auto object = winrt::make_self<ObservedObject>();
        auto property = DependencyProperty<int>::Register<ObservedObject>("Validated", 0,
            PropertyEffects::None, +[](int)
            {
                auto callback = std::exchange(propertyValidation, {});
                if (callback) { callback(); }
                return true;
            });
        propertyValidation = [&] { object->SetValue(property, 7); };
        object->SetValue(property, 7);
        Require(object->notifications == 1, "Validation must compare against the latest committed value after reentry.");
        auto weak = object->DependencyObject::get_weak();
        propertyValidation = [&]
        {
            object = nullptr;
            Require(static_cast<bool>(weak.get()), "Validation must retain its native owner.");
        };
        auto* raw = object.get();
        raw->SetValue(property, 9);
        Require(!weak.get(), "The validation operation must release its owner after completing.");
    }

    void ArrangeRetainsTheOwnerAcrossImplicitMeasure()
    {
        auto element = winrt::make_self<MeasuredElement>();
        auto weak = element->DependencyObject::get_weak();
        element->measureCallback = [&] { element = nullptr; };
        auto* raw = element.get();
        raw->Arrange({ 0, 0, 100, 50 });
        Require(!weak.get(), "Arrange must retain its owner until implicit measure and arrange both finish.");
    }

    void PointerCancellationAndOtherPointersDoNotClick()
    {
        auto button = winrt::make_self<Button>();
        int clicks{};
        (void)button->AddClick([&](auto&) { ++clicks; });
        button->ProcessPointer(Pointer(PointerAction::Pressed));
        Require(!button->ProcessPointer(Pointer(PointerAction::Released, true, 2)), "Another pointer must not end the active gesture.");
        button->ProcessPointer(Pointer(PointerAction::Moved, false));
        Require(!button->IsPressed(), "Moving outside must remove the pressed visual.");
        button->ProcessPointer(Pointer(PointerAction::Released, false));
        Require(clicks == 0, "Release outside must not click.");
        button->ProcessPointer(Pointer(PointerAction::Pressed));
        button->ProcessPointer(Pointer(PointerAction::CaptureLost));
        button->ProcessPointer(Pointer(PointerAction::Released));
        Require(clicks == 0 && !button->ActivePointer(), "Capture loss must cancel activation.");
        Require(!button->ProcessPointer({ PointerAction::Pressed, 1, true, false }), "Secondary button press must be ignored.");
    }

    void PressAndHoverModesHaveDistinctTransitions()
    {
        auto button = winrt::make_self<Button>();
        int clicks{};
        (void)button->AddClick([&](auto&) { ++clicks; });
        button->ClickMode(ClickMode::Press);
        button->ProcessPointer(Pointer(PointerAction::Pressed));
        button->ProcessPointer(Pointer(PointerAction::Released));
        Require(clicks == 1, "Press mode must click on press without a second release click.");
        button->ClickMode(ClickMode::Hover);
        button->ProcessPointer(Pointer(PointerAction::Entered));
        button->ProcessPointer(Pointer(PointerAction::Entered));
        Require(clicks == 2 && button->IsPressed(), "Hover must activate once per entry.");
        button->ProcessPointer(Pointer(PointerAction::Exited, false));
        button->ProcessPointer(Pointer(PointerAction::Entered));
        Require(clicks == 3, "Reentry must permit the next hover activation.");
        Rejects([&] { button->ClickMode(static_cast<ClickMode>(99)); });
    }

    void KeyboardActivationRequiresFocusAndDoesNotRepeat()
    {
        auto button = winrt::make_self<Button>();
        int clicks{};
        (void)button->AddClick([&](auto&) { ++clicks; });
        Require(!button->ProcessKey(ActivationKey::Space, true), "Unfocused buttons must ignore activation keys.");
        button->SetFocused(true);
        for (auto key : { ActivationKey::Space, ActivationKey::Enter })
        {
            button->ProcessKey(key, true);
            button->ProcessKey(key, true);
            Require(button->IsPressed(), "Activation key down must set the pressed state.");
            button->ProcessKey(key, false);
        }
        Require(clicks == 2, "Each key gesture must produce only one release activation.");
        button->ClickMode(ClickMode::Press);
        button->ProcessKey(ActivationKey::Space, true);
        button->ProcessKey(ActivationKey::Space, true);
        button->ProcessKey(ActivationKey::Space, false);
        Require(clicks == 3, "Repeated keydown must not repeat a press-mode click.");
    }

    void DisableFocusLossAndDetachmentCancelInput()
    {
        auto parent = winrt::make_self<ContentControl>();
        auto button = winrt::make_self<Button>();
        int clicks{};
        (void)button->AddClick([&](auto&) { ++clicks; });
        parent->Content(button.get());
        button->ProcessPointer(Pointer(PointerAction::Pressed));
        parent->IsEnabled(false);
        Require(!button->IsEnabled() && !button->IsPressed() && !button->IsFocused() && !button->ActivePointer(),
            "Ancestor disable must clear input and focus throughout the subtree.");
        parent->IsEnabled(true);
        button->ProcessPointer(Pointer(PointerAction::Released));
        Require(clicks == 0, "Reenable must not revive the cancelled press.");
        button->SetFocused(true);
        button->ProcessKey(ActivationKey::Space, true);
        button->SetFocused(false);
        Require(!button->ProcessKey(ActivationKey::Space, false) && clicks == 0, "Focus loss must cancel keyboard activation.");
        button->ProcessPointer(Pointer(PointerAction::Pressed));
        parent->Content(nullptr);
        Require(!button->IsPressed() && !button->ActivePointer(), "Detachment must cancel a pending gesture.");
    }

    void ClickHandlersCanMutateRegistrationAndReenter()
    {
        auto button = winrt::make_self<Button>();
        int first{};
        int removed{};
        int added{};
        ButtonBase::EventToken removeToken{};
        (void)button->AddClick([&](auto& sender)
        {
            ++first;
            sender.RemoveClick(removeToken);
            (void)sender.AddClick([&](auto&) { ++added; });
            sender.IsEnabled(false);
        });
        removeToken = button->AddClick([&](auto&) { ++removed; });
        button->ProcessPointer(Pointer(PointerAction::Pressed));
        button->ProcessPointer(Pointer(PointerAction::Released));
        Require(first == 1 && removed == 0 && added == 0 && !button->IsPressed(),
            "Event removal must take effect immediately; additions wait for the next raise.");
        button->IsEnabled(true);
        button->ProcessPointer(Pointer(PointerAction::Pressed));
        button->ProcessPointer(Pointer(PointerAction::Released));
        Require(first == 2 && added == 1, "A later gesture must see previously added handlers.");
    }

    void ClickSurvivesRemovalOfTheLastExternalOwner()
    {
        auto parent = winrt::make_self<ContentControl>();
        auto button = winrt::make_self<Button>();
        parent->Content(button.get());
        auto weak = button->DependencyObject::get_weak();
        bool finished{};
        (void)button->AddClick([&](auto& sender)
        {
            parent->Content(nullptr);
            button = nullptr;
            Require(sender.HasThreadAccess(), "The active callback must keep its sender alive.");
            finished = true;
        });
        button->ProcessPointer(Pointer(PointerAction::Pressed));
        auto* raw = button.get();
        raw->ProcessPointer(Pointer(PointerAction::Released));
        Require(finished && !weak.get(), "The sender must die only after dispatch and its native operation finish.");
    }

    void ThrowingClickLeavesReleaseCommitted()
    {
        auto button = winrt::make_self<Button>();
        auto token = button->AddClick([](auto&) { throw std::runtime_error("Click failed."); });
        button->ProcessPointer(Pointer(PointerAction::Pressed));
        Rejects<std::runtime_error>([&] { button->ProcessPointer(Pointer(PointerAction::Released)); });
        Require(!button->IsPressed() && !button->ActivePointer(), "A throwing handler must not leave released input pressed.");
        button->RemoveClick(token);
        Require(button->ProcessPointer(Pointer(PointerAction::Pressed)), "The next gesture must remain usable.");
    }

    void WrongThreadCannotMutateTheControlKernel()
    {
        auto button = winrt::make_self<Button>();
        std::exception_ptr failure;
        std::jthread worker([&]
        {
            try
            {
                Rejects<winrt::hresult_wrong_thread>([&] { button->Width(30); });
                Rejects<winrt::hresult_wrong_thread>([&] { button->Content(nullptr); });
                Rejects<winrt::hresult_wrong_thread>([&] { button->Measure({ 10, 10 }); });
                Rejects<winrt::hresult_wrong_thread>([&] { button->ProcessPointer(Pointer(PointerAction::Pressed)); });
                Rejects<winrt::hresult_wrong_thread>([&] { (void)button->AddClick([](auto&) {}); });
            }
            catch (...) { failure = std::current_exception(); }
        });
        worker.join();
        if (failure) { std::rethrow_exception(failure); }
    }

    void PanelChildrenShareOneNativeIdentityAndParent()
    {
        auto panel = winrt::make_self<Panel>();
        auto first = winrt::make_self<Button>();
        auto second = winrt::make_self<FrameworkElement>();
        panel->Append(first.get());
        panel->Insert(0, second.get());
        Require(panel->ChildrenCount() == 2 && panel->ChildAt(0).get() == second.get() &&
            panel->ChildAt(1).get() == first.get(), "Panel insertion must preserve ordered native identities.");
        Require(first->Parent().get() == panel.get() && second->Parent().get() == panel.get(),
            "Every child must have the same canonical parent.");
        auto other = winrt::make_self<Panel>();
        Rejects([&] { other->Append(first.get()); });
        Rejects([&] { panel->Append(first.get()); });
        Rejects([&] { panel->Append(nullptr); });
        Rejects([&] { panel->Append(panel.get()); });
        Rejects<winrt::hresult_out_of_bounds>([&] { panel->Insert(4, second.get()); });
        Require(panel->ChildrenCount() == 2 && other->ChildrenCount() == 0,
            "Rejected mutations must not modify either tree.");
        auto nested = winrt::make_self<Panel>();
        panel->Append(nested.get());
        Rejects([&] { nested->Append(panel.get()); });
        Require(!panel->Parent() && nested->Parent().get() == panel.get(),
            "Ancestor cycles must be rejected before publishing a visual edge.");
    }

    void PanelMovesDoNotDetachRetainedChildren()
    {
        auto panel = winrt::make_self<Panel>();
        auto first = winrt::make_self<ReparentingElement>();
        auto second = winrt::make_self<ReparentingElement>();
        int changes{};
        first->parentChanged = [&] { ++changes; };
        second->parentChanged = [&] { ++changes; };
        panel->Append(first.get());
        panel->Append(second.get());
        Require(changes == 2, "Attaching each child must notify once.");
        panel->Move(0, 1);
        Require(panel->ChildAt(0).get() == second.get() && panel->ChildAt(1).get() == first.get(),
            "Move must update order without changing element identity.");
        Require(changes == 2 && first->Parent().get() == panel.get(),
            "Moving retained children must not raise detach notifications.");
        panel->Move(1, 0);
        panel->Move(0, 0);
        Rejects<winrt::hresult_out_of_bounds>([&] { panel->Move(0, 9); });
        panel->RemoveAt(0);
        Require(!first->Parent() && second->Parent().get() == panel.get(),
            "Removal must clear only the removed child's parent.");
        panel->Clear();
        Require(!second->Parent() && panel->ChildrenCount() == 0, "Clear must detach all children.");
        Rejects<winrt::hresult_out_of_bounds>([&] { panel->RemoveAt(0); });
    }

    void PanelRetainsChildrenThroughReentrantRemoval()
    {
        auto panel = winrt::make_self<Panel>();
        auto first = winrt::make_self<ReparentingElement>();
        auto second = winrt::make_self<FrameworkElement>();
        panel->Append(first.get());
        first->parentChanged = [&] { panel->Append(second.get()); };
        panel->RemoveAt(0);
        Require(panel->ChildrenCount() == 1 && panel->ChildAt(0).get() == second.get() &&
            second->Parent().get() == panel.get() && !first->Parent(),
            "Nested mutation must remain authoritative after the outer removal returns.");
    }

    void PanelStrongEdgesOwnTheirChildren()
    {
        auto panel = winrt::make_self<Panel>();
        auto first = winrt::make_self<FrameworkElement>();
        auto second = winrt::make_self<FrameworkElement>();
        auto weakFirst = first->DependencyObject::get_weak();
        auto weakSecond = second->DependencyObject::get_weak();
        panel->Append(first.get());
        panel->Append(second.get());
        first = nullptr;
        second = nullptr;
        Require(weakFirst.get() && weakSecond.get(), "Native panel child edges must keep children alive.");
        panel->RemoveAt(0);
        Require(!weakFirst.get() && weakSecond.get(), "Removing one edge must release only its child.");
        panel = nullptr;
        Require(!weakSecond.get(), "Destroying the panel must release remaining children.");
    }

    void ButtonPaddingMeasuresAndArrangesItsContent()
    {
        auto button = winrt::make_self<Button>();
        auto child = winrt::make_self<MeasuredElement>();
        button->Content(child.get());
        Require(button->Padding() == Thickness{}, "Button padding must default to zero before a theme is applied.");
        button->Padding({ 4, 5, 6, 7 });
        Require(button->HasLocalValue(Control::PaddingProperty), "Padding must use the canonical property store.");
        button->Measure({ 100, 80 });
        Require(child->lastAvailable == Size{ 90, 68 } && button->DesiredSize() == Size{ 30, 22 },
            "Padding must reduce the content constraint and increase the button's desired size.");
        button->Arrange({ 0, 0, 100, 80 });
        Require(child->LayoutSlot() == Rect{ 4, 5, 90, 68 },
            "Padding must position the child within the content rectangle.");
        auto const version = button->RenderVersion();
        button->Padding({ 4, 5, 6, 7 });
        Require(button->IsMeasureValid() && button->RenderVersion() == version,
            "Writing unchanged padding must not trigger an additional layout pass.");
        button->Padding({ 10, 5, 6, 7 });
        Require(!button->IsMeasureValid() && button->RenderVersion() > version,
            "Changing padding must invalidate both layout and rendering.");
        Rejects([&] { button->Padding({ -1, 0, 0, 0 }); });
        Rejects([&] { button->Padding({ std::numeric_limits<double>::infinity(), 0, 0, 0 }); });
        Rejects([&] { button->Padding({ 1e308, 0, 1e308, 0 }); });
        Require(button->Padding() == Thickness{ 10, 5, 6, 7 },
            "An invalid padding write must retain the previously committed value.");
        button->ClearValue(Control::PaddingProperty);
        Require(button->Padding() == Thickness{} && !button->HasLocalValue(Control::PaddingProperty),
            "Clearing padding must restore its default.");
        auto empty = winrt::make_self<Button>();
        empty->Padding({ 2, 3, 4, 5 });
        empty->Measure({ 100, 100 });
        Require(empty->DesiredSize() == Size{ 6, 8 }, "An empty button must retain its padding size.");
    }

    void StackPanelMeasuresAndArrangesBothOrientations()
    {
        auto panel = winrt::make_self<StackPanel>();
        auto first = winrt::make_self<MeasuredElement>();
        auto second = winrt::make_self<MeasuredElement>();
        panel->Append(first.get());
        panel->Append(second.get());
        panel->Spacing(3);
        panel->Measure({ 100, 60 });
        Require(panel->DesiredSize() == Size{ 20, 23 }, "Vertical layout must sum heights and spacing.");
        panel->Arrange({ 2, 4, 100, 60 });
        Require(first->LayoutSlot() == Rect{ 0, 0, 100, 10 } && second->LayoutSlot() == Rect{ 0, 13, 100, 10 },
            "Vertical layout must position children in sequence.");
        panel->Orientation(Orientation::Horizontal);
        Require(!panel->IsMeasureValid(), "Orientation changes must invalidate measure.");
        panel->Measure({ 100, 60 });
        Require(panel->DesiredSize() == Size{ 43, 10 }, "Horizontal layout must sum widths and spacing.");
        panel->Arrange({ 0, 0, 100, 60 });
        Require(first->LayoutSlot() == Rect{ 0, 0, 20, 60 } && second->LayoutSlot() == Rect{ 23, 0, 20, 60 },
            "Horizontal layout must position children in sequence.");
        Rejects([&] { panel->Spacing(-1); });
        Rejects([&] { panel->Spacing(std::numeric_limits<double>::infinity()); });
        Rejects([&] { panel->Orientation(static_cast<Orientation>(20)); });
        Require(panel->Spacing() == 3 && panel->Orientation() == Orientation::Horizontal,
            "Invalid property values must not affect committed layout configuration.");
    }

    void StackPanelRetainsInvalidationsFromReentrantLayout()
    {
        auto panel = winrt::make_self<StackPanel>();
        auto first = winrt::make_self<MeasuredElement>();
        auto second = winrt::make_self<MeasuredElement>();
        panel->Append(first.get());
        first->measureCallback = [&]
        {
            first->measureCallback = {};
            panel->Append(second.get());
        };
        panel->Measure({ 100, 100 });
        Require(!panel->IsMeasureValid(), "Reentrant child insertion must keep the parent dirty.");
        panel->Measure({ 100, 100 });
        Require(panel->IsMeasureValid() && panel->DesiredSize() == Size{ 20, 20 },
            "The next layout pass must observe the newly inserted child.");
    }

    void PanelRejectsWrongThreadMutations()
    {
        auto panel = winrt::make_self<Panel>();
        auto child = winrt::make_self<FrameworkElement>();
        std::exception_ptr failure;
        std::jthread worker([&]
        {
            try
            {
                Rejects<winrt::hresult_wrong_thread>([&] { panel->Append(child.get()); });
                Rejects<winrt::hresult_wrong_thread>([&] { panel->Clear(); });
                Rejects<winrt::hresult_wrong_thread>([&] { panel->Move(0, 0); });
            }
            catch (...) { failure = std::current_exception(); }
        });
        worker.join();
        if (failure) { std::rethrow_exception(failure); }
        Require(panel->ChildrenCount() == 0, "Wrong-thread calls must not mutate the tree.");
    }

    void DetachingAnAncestorCancelsNestedInput()
    {
        auto host = winrt::make_self<ContentControl>();
        auto panel = winrt::make_self<Panel>();
        auto button = winrt::make_self<Button>();
        int clicks{};
        (void)button->AddClick([&](auto&) { ++clicks; });
        panel->Append(button.get());
        host->Content(panel.get());
        button->ProcessPointer(Pointer(PointerAction::Pressed));
        Require(button->IsPressed() && button->ActivePointer(), "The nested button must have an active press.");
        host->Content(nullptr);
        Require(!button->IsFocused() && !button->IsPressed() && !button->ActivePointer(),
            "Detaching a subtree must cancel input and focus held by its descendants.");
        host->Content(panel.get());
        button->ProcessPointer(Pointer(PointerAction::Released));
        Require(clicks == 0, "Reattaching must not revive an abandoned gesture.");
    }

    void FrameworkElementConstraintsAndMarginsParticipateInLayout()
    {
        auto element = winrt::make_self<MeasuredElement>();
        Require(element->MinWidth() == 0 && std::isinf(element->MaxWidth()) &&
            element->MinHeight() == 0 && std::isinf(element->MaxHeight()),
            "FrameworkElement constraints must use XAML defaults.");
        element->MinWidth(30);
        element->MaxWidth(15);
        element->MaxHeight(7);
        element->Margin({ 5, 3, 7, 4 });
        element->Measure({ 100, 100 });
        Require(element->lastAvailable == Size{ 30, 7 }, "MeasureOverride must see constrained content space.");
        Require(element->DesiredSize() == Size{ 42, 14 }, "MinWidth must win over MaxWidth and margins add to desired size.");
        element->Arrange({ 0, 0, 60, 40 });
        Require(element->ActualSize() == Size{ 30, 7 }, "Arranged content must exclude margins and honor constraints.");
        Rejects([&] { element->MinWidth(-1); });
        Rejects([&] { element->MinHeight(std::numeric_limits<double>::infinity()); });
        Rejects([&] { element->MaxWidth(std::numeric_limits<double>::quiet_NaN()); });
        Rejects([&] { element->Margin({ 0, std::numeric_limits<double>::infinity(), 0, 0 }); });
        Require(element->Margin() == Thickness{ 5, 3, 7, 4 }, "Invalid margins must preserve local state.");
    }

    void CollapsedElementsSkipLayoutOverridesUntilVisible()
    {
        auto element = winrt::make_self<MeasuredElement>();
        element->Measure({ 100, 60 });
        element->Arrange({ 0, 0, 100, 60 });
        auto const measures = element->measures;
        auto const arranges = element->arranges;
        element->VisibilityState(Visibility::Collapsed);
        Require(!element->IsMeasureValid(), "Collapsing must invalidate layout.");
        element->Measure({ 100, 60 });
        element->Arrange({ 0, 0, 100, 60 });
        Require(element->DesiredSize() == Size{} && element->ActualSize() == Size{},
            "Collapsed elements must occupy no space.");
        Require(element->measures == measures && element->arranges == arranges,
            "Collapsed elements must not call layout overrides.");
        Rejects([&] { element->VisibilityState(static_cast<Visibility>(40)); });
        element->VisibilityState(Visibility::Visible);
        element->Measure({ 100, 60 });
        element->Arrange({ 0, 0, 100, 60 });
        Require(element->measures == measures + 1 && element->arranges == arranges + 1,
            "Restoring visibility must rerun both layout overrides.");
    }

    void FrameworkElementAlignmentPositionsContentWithinLayoutSlot()
    {
        auto element = winrt::make_self<MeasuredElement>();
        element->Margin({ 5, 3, 7, 4 });
        element->HorizontalAlignmentState(HorizontalAlignment::Left);
        element->VerticalAlignmentState(VerticalAlignment::Top);
        element->Measure({ 100, 80 });
        element->Arrange({ 10, 20, 100, 80 });
        Require(element->LayoutBounds() == Rect{ 15, 23, 20, 10 },
            "Left/top alignment must include the parent's slot origin and margins.");

        element->HorizontalAlignmentState(HorizontalAlignment::Right);
        element->VerticalAlignmentState(VerticalAlignment::Bottom);
        element->Arrange({ 10, 20, 100, 80 });
        Require(element->LayoutBounds() == Rect{ 83, 86, 20, 10 },
            "Right/bottom alignment must position content within the remaining space.");

        element->HorizontalAlignmentState(HorizontalAlignment::Center);
        element->VerticalAlignmentState(VerticalAlignment::Center);
        element->Arrange({ 10, 20, 100, 80 });
        Require(element->LayoutBounds() == Rect{ 49, 55, 20, 10 },
            "Center alignment must align the remaining free space to device pixels.");

        element->HorizontalAlignmentState(HorizontalAlignment::Stretch);
        element->VerticalAlignmentState(VerticalAlignment::Stretch);
        element->Arrange({ 10, 20, 100, 80 });
        Require(element->LayoutBounds() == Rect{ 15, 23, 88, 73 },
            "Stretch alignment must fill the available content space.");

        element->Width(25);
        element->Height(12);
        element->Measure({ 100, 80 });
        element->Arrange({ 10, 20, 100, 80 });
        Require(element->LayoutBounds() == Rect{ 15, 23, 25, 12 },
            "Explicit dimensions must override stretch sizing.");
        Rejects([&] { element->HorizontalAlignmentState(static_cast<HorizontalAlignment>(99)); });
        Rejects([&] { element->VerticalAlignmentState(static_cast<VerticalAlignment>(99)); });
    }

    void OpacityUpdatesRenderingWithoutInvalidatingLayout()
    {
        auto element = winrt::make_self<MeasuredElement>();
        Require(element->Opacity() == 1.0, "Opacity must default to fully opaque.");
        element->Measure({ 100, 50 });
        element->Arrange({ 0, 0, 100, 50 });
        auto const before = element->RenderVersion();
        element->Opacity(0.5);
        Require(element->Opacity() == 0.5 && element->RenderVersion() > before &&
            element->IsMeasureValid() && element->IsArrangeValid(),
            "Opacity changes must invalidate the visual without rerunning layout.");
        Rejects([&] { element->Opacity(-0.1); });
        Rejects([&] { element->Opacity(1.1); });
        Rejects([&] { element->Opacity(std::numeric_limits<double>::quiet_NaN()); });
        Require(element->Opacity() == 0.5, "Rejected opacity values must preserve state.");
    }

    void ContentRootHandlesExplicitInvalidationWhileRendering()
    {
        auto element = winrt::make_self<MeasuredElement>();
        winrt::com_ptr<UIElement> hosted;
        hosted.copy_from(static_cast<UIElement*>(element.get()));
        std::deque<std::function<void()>> callbacks;
        auto enqueue = [&](std::function<void()> callback)
        {
            callbacks.push_back(std::move(callback));
            return true;
        };
        std::shared_ptr<ContentRoot> root;
        int renders{};
        root = ContentRoot::Create(hosted, { 50, 50 }, enqueue, [&](UIElement&)
        {
            if (++renders == 1) { root->Invalidate(); }
        });
        auto next = std::move(callbacks.front());
        callbacks.pop_front();
        next();
        Require(renders == 2 && root->FrameCount() == 1 && callbacks.empty(),
            "An explicit invalidation during rendering must be processed before the frame settles.");
        root->Shutdown();
    }

    void ContentRootSchedulesOneFrameAndDisconnectsSafely()
    {
        auto parent = winrt::make_self<ContentControl>();
        auto child = winrt::make_self<MeasuredElement>();
        parent->Content(child.get());
        std::deque<std::function<void()>> callbacks;
        int rendered{};
        auto enqueue = [&](std::function<void()> callback)
        {
            callbacks.push_back(std::move(callback));
            return true;
        };
        winrt::com_ptr<UIElement> hosted;
        hosted.copy_from(static_cast<UIElement*>(parent.get()));
        auto root = ContentRoot::Create(hosted, { 100, 60 }, enqueue,
            [&](UIElement&) { ++rendered; });
        Require(callbacks.size() == 1, "Content root must schedule its initial update.");
        child->Width(40);
        child->Height(20);
        child->InvalidateRender();
        Require(callbacks.size() == 1, "Dirty child updates must coalesce into one UI frame.");
        auto next = std::move(callbacks.front());
        callbacks.pop_front();
        next();
        Require(root->FrameCount() == 1 && rendered == 1 &&
            parent->IsArrangeValid() && child->IsArrangeValid(),
            "The frame must run layout before rendering exactly once.");
        Require(callbacks.empty(), "Layout invalidation during the frame must not schedule redundant work.");

        root->Resize({ 150, 80 });
        Require(callbacks.size() == 1, "Resizing a live root must schedule a frame.");
        root->Shutdown();
        while (!callbacks.empty())
        {
            next = std::move(callbacks.front());
            callbacks.pop_front();
            next();
        }
        child->Width(50);
        Require(rendered == 1 && callbacks.empty(),
            "Shutdown must cancel pending frames and disconnect invalidation callbacks.");
        Rejects<winrt::hresult_illegal_method_call>([&] { root->Resize({ 200, 100 }); });
    }

    void ContentRootRejectsDoubleAttachmentAndReparentsAfterShutdown()
    {
        auto element = winrt::make_self<MeasuredElement>();
        std::deque<std::function<void()>> callbacks;
        auto enqueue = [&](std::function<void()> callback)
        {
            callbacks.push_back(std::move(callback));
            return true;
        };
        winrt::com_ptr<UIElement> hosted;
        hosted.copy_from(static_cast<UIElement*>(element.get()));
        auto root = ContentRoot::Create(hosted, { 100, 80 }, enqueue,
            [](UIElement&) {});
        Rejects([&] { (void)ContentRoot::Create(hosted, { 100, 80 }, enqueue,
            [](UIElement&) {}); });
        auto panel = winrt::make_self<Panel>();
        Rejects([&] { panel->Append(element.get()); });
        root->Shutdown();
        panel->Append(element.get());
        Require(element->Parent().get() == panel.get(), "Shut down roots can return to ordinary visual ownership.");
    }

    struct LifecycleElement : winrt::implements<LifecycleElement, MeasuredElement>
    {
        std::vector<std::string>* notifications{};
        std::atomic_int* destroyed{};
        std::string label;
        bool invalidateOnLoad{};

        ~LifecycleElement() override { if (destroyed) { ++*destroyed; } }

        void OnLoading() override { notifications->push_back(label + ":Loading"); }
        void OnLoaded() override
        {
            notifications->push_back(label + ":Loaded");
            if (invalidateOnLoad)
            {
                invalidateOnLoad = false;
                Width(42);
            }
        }
        void OnUnloaded() override { notifications->push_back(label + ":Unloaded"); }
    };

    void LoadingIsOncePerElementEvenWhenMeasuredBeforeAttachment()
    {
        auto element = winrt::make_self<LifecycleElement>();
        std::vector<std::string> notifications;
        element->notifications = &notifications;
        element->label = "detached";
        element->Measure({ 50, 40 });
        Require(notifications == std::vector<std::string>{ "detached:Loading" } &&
            !element->IsConnectedToRoot() && !element->IsLoaded(),
            "The first offline Measure must raise Loading without raising Loaded.");
        element->InvalidateMeasure();
        element->Measure({ 50, 40 });
        Require(notifications.size() == 1, "A repeated Measure must not repeat Loading.");

        std::deque<std::function<void()>> callbacks;
        auto enqueue = [&](std::function<void()> callback)
        {
            callbacks.push_back(std::move(callback));
            return true;
        };
        winrt::com_ptr<UIElement> hosted;
        hosted.copy_from(static_cast<UIElement*>(element.get()));
        auto root = ContentRoot::Create(hosted, { 50, 40 }, enqueue, [](UIElement&) {});
        auto next = std::move(callbacks.front());
        callbacks.pop_front();
        next();
        Require(notifications == std::vector<std::string>{ "detached:Loading", "detached:Loaded" },
            "Attaching a previously measured element must raise Loaded without replaying Loading.");
        root->Shutdown();
        Require(notifications.back() == "detached:Loaded", "Shutdown must defer Unloaded to the UI queue.");
        next = std::move(callbacks.front());
        callbacks.pop_front();
        next();
        Require(notifications.back() == "detached:Unloaded", "The posted Unloaded event must run after shutdown.");
        root = ContentRoot::Create(hosted, { 50, 40 }, enqueue, [](UIElement&) {});
        next = std::move(callbacks.front());
        callbacks.pop_front();
        next();
        Require(std::ranges::count(notifications, "detached:Loading") == 1 &&
            std::ranges::count(notifications, "detached:Loaded") == 2,
            "Reconnect must not repeat the lifetime's first Loading event.");
        root->Shutdown();
        while (!callbacks.empty())
        {
            next = std::move(callbacks.front());
            callbacks.pop_front();
            next();
        }
    }

    void DeferredUnloadRetainsDetachedElementsUntilDelivery()
    {
        std::vector<std::string> notifications;
        std::atomic_int destroyed{};
        std::deque<std::function<void()>> callbacks;
        auto enqueue = [&](std::function<void()> callback)
        {
            callbacks.push_back(std::move(callback));
            return true;
        };
        auto element = winrt::make_self<LifecycleElement>();
        element->notifications = &notifications;
        element->destroyed = &destroyed;
        element->label = "released";
        winrt::com_ptr<UIElement> hosted;
        hosted.copy_from(static_cast<UIElement*>(element.get()));
        auto root = ContentRoot::Create(hosted, { 20, 20 }, enqueue, [](UIElement&) {});
        auto next = std::move(callbacks.front());
        callbacks.pop_front();
        next();
        Require(notifications == std::vector<std::string>{ "released:Loading", "released:Loaded" },
            "The first frame must load the element before detach.");
        root->Shutdown();
        element = nullptr;
        hosted = nullptr;
        root = nullptr;
        Require(destroyed == 0 && notifications.back() == "released:Loaded" && callbacks.size() == 1,
            "A queued Unloaded event must own the otherwise released element.");
        next = std::move(callbacks.front());
        callbacks.pop_front();
        next();
        next = {};
        Require(destroyed == 1 && notifications.back() == "released:Unloaded",
            "Unloaded must run exactly once before releasing the last reference.");
    }

    void RootLifecycleFollowsAttachmentLayoutAndDetach()
    {
        auto panel = winrt::make_self<StackPanel>();
        auto first = winrt::make_self<LifecycleElement>();
        auto second = winrt::make_self<LifecycleElement>();
        std::vector<std::string> notifications;
        first->notifications = &notifications;
        second->notifications = &notifications;
        first->label = "first";
        second->label = "second";
        first->invalidateOnLoad = true;
        panel->Append(first.get());
        std::deque<std::function<void()>> callbacks;
        auto enqueue = [&](std::function<void()> callback)
        {
            callbacks.push_back(std::move(callback));
            return true;
        };
        winrt::com_ptr<UIElement> hosted;
        hosted.copy_from(static_cast<UIElement*>(panel.get()));
        auto root = ContentRoot::Create(hosted, { 100, 60 }, enqueue, [](UIElement&) {});
        Require(notifications.empty() &&
            first->IsConnectedToRoot() && !first->IsLoaded(),
            "Attaching a root must defer Loading until the first measure.");
        auto next = std::move(callbacks.front());
        callbacks.pop_front();
        next();
        Require(first->IsLoaded() && first->Width() == 42 &&
            notifications == std::vector<std::string>{ "first:Loading", "first:Loaded" } &&
            root->FrameCount() == 1,
            "Loaded must fire exactly once and its layout mutations must settle before rendering.");

        panel->Append(second.get());
        Require(second->IsConnectedToRoot() && !second->IsLoaded() &&
            notifications.back() == "first:Loaded", "Attaching must not raise Loading before measure.");
        next = std::move(callbacks.front());
        callbacks.pop_front();
        next();
        Require(second->IsLoaded() && notifications.back() == "second:Loaded",
            "Dynamically attached children must become Loaded after layout.");

        panel->RemoveAt(0);
        Require(!first->IsLoaded() && !first->IsConnectedToRoot() &&
            notifications.back() == "second:Loaded", "Detaching must defer Unloaded until the dispatcher runs.");
        root->Shutdown();
        Require(!second->IsLoaded() && notifications.back() == "second:Loaded",
            "Root shutdown must also defer Unloaded for remaining descendants.");
        root->Shutdown();
        while (!callbacks.empty())
        {
            next = std::move(callbacks.front());
            callbacks.pop_front();
            next();
        }
        Require(std::ranges::count(notifications, "first:Unloaded") == 1 &&
            std::ranges::count(notifications, "second:Unloaded") == 1,
            "Detached and shutdown elements must receive one deferred Unloaded event each.");
    }
}

int RunControlTests()
{
    using Test = void (*)();
    std::array<Test, 65> const tests{
        MetadataFactoriesAreLazyScopedAndValidated,
        ErasedPropertiesShareTypedStorageAndMetadata,
        NonGenericRegistrationSharesPropertyStorageAndMetadata,
        BoxedMetadataFactoriesAreLazyPerOwnerAndObservable,
        TypedMetadataInspectionPreservesCallbacksAndTypeValidation,
        GeneratedDefaultKeepsDynamicRegistrationMetadataAlive,
        LayoutRoundingUsesPhysicalPixelBoundaries,
        LayoutRoundingInheritsAcrossVisualTreeAndRespectsOverrides,
        HitTestingFollowsCompositionOrderAndVisibility,
        FlowDirectionInheritsAndMirrorsHitTesting,
        LifecycleEventSubscriptionsRespectMutationAndDeferredDelivery,
        SynchronousUpdateLayoutConsumesQueuedFrameWithoutReentering,
        ContentRootPropagatesRasterizationScaleToChildren,
        RejectedDispatcherRollsBackRootAttachment,
        EnumPropertiesSupportErasedBoxedAccess,
        BoxedStringAndInt64PropertiesPreserveValueTypes,
        PropertyValuePrecedencePreservesLayersAndOnlyNotifiesEffectiveChanges,
        ClipBoundsInvalidatesRenderingWithoutChangingLayout,
        SizeChangedHandlersObserveCommittedLayoutAndSafeRevocation,
        LoadingIsOncePerElementEvenWhenMeasuredBeforeAttachment,
        DeferredUnloadRetainsDetachedElementsUntilDelivery,
        OpacityUpdatesRenderingWithoutInvalidatingLayout,
        RootLifecycleFollowsAttachmentLayoutAndDetach,
        FrameworkElementAlignmentPositionsContentWithinLayoutSlot,
        ContentRootHandlesExplicitInvalidationWhileRendering,
        WinRTReferencePropertiesRetainAndReleaseValues,
        ContentRootSchedulesOneFrameAndDisconnectsSafely,
        ContentRootRejectsDoubleAttachmentAndReparentsAfterShutdown,
        PropertyStorageAndValidation, AttachedPropertyAcceptsAnyDependencyObject,
        PropertyMetadataOutlivesRegistrationHandle, PropertyChangedCallbacksArePropertyScopedAndRevocable,
        PropertyChangedCallbacksSupportReentrancyAndMutation,
        PropertyMetadataCallbacksObserveCommittedOldAndNewValues,
        PropertyNotificationsSeeCommittedState, SingleParentAndCycleChecksAreAtomic,
        NativeTreeOwnsChildrenWithoutOwningItsParent, ContentParticipatesInLayoutAndInvalidation,
        LayoutRetainsInvalidationAndRecoversAfterExceptions, PointerReleaseUsesOneObjectAndOneClick,
        PointerCancellationAndOtherPointersDoNotClick, PressAndHoverModesHaveDistinctTransitions,
        KeyboardActivationRequiresFocusAndDoesNotRepeat, DisableFocusLossAndDetachmentCancelInput,
        ClickHandlersCanMutateRegistrationAndReenter, ClickSurvivesRemovalOfTheLastExternalOwner,
        ThrowingClickLeavesReleaseCommitted, WrongThreadCannotMutateTheControlKernel,
        ArrangeAndRenderInvalidationReachAncestors, LayoutRejectsRecursiveAndInvalidOverrides,
        PropertyChangePinsItsNativeOwner, ReentrantContentChangesRetainTheNewestTree,
        ValidationRetainsTheOwnerAndSeesNestedWrites, ArrangeRetainsTheOwnerAcrossImplicitMeasure,
        PanelChildrenShareOneNativeIdentityAndParent, PanelMovesDoNotDetachRetainedChildren,
        PanelRetainsChildrenThroughReentrantRemoval, PanelStrongEdgesOwnTheirChildren,
        ButtonPaddingMeasuresAndArrangesItsContent, StackPanelMeasuresAndArrangesBothOrientations,
        StackPanelRetainsInvalidationsFromReentrantLayout,
        PanelRejectsWrongThreadMutations, DetachingAnAncestorCancelsNestedInput,
        FrameworkElementConstraintsAndMarginsParticipateInLayout,
        CollapsedElementsSkipLayoutOverridesUntilVisible
    };
    for (std::size_t index = 0; index < tests.size(); ++index)
    {
        try { tests[index](); }
        catch (...)
        {
            std::cerr << "Control test failed at index " << index << '\n';
            throw;
        }
    }
    return static_cast<int>(tests.size());
}
