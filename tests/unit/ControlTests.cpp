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
        auto weakChild = child->Object::get_weak();
        parent->Content(child.get());
        child = nullptr;
        Require(static_cast<bool>(weakChild.get()), "A parent must own its child.");
        parent->Content(nullptr);
        Require(!weakChild.get(), "Removing the only owning edge must release the child.");
        child = winrt::make_self<FrameworkElement>();
        parent->Content(child.get());
        auto weakParent = parent->Object::get_weak();
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
        auto weak = object->Object::get_weak();
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
        auto weak = object->Object::get_weak();
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
        auto weak = element->Object::get_weak();
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
        auto weak = button->Object::get_weak();
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
        auto weakFirst = first->Object::get_weak();
        auto weakSecond = second->Object::get_weak();
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
}

int RunControlTests()
{
    using Test = void (*)();
    std::array<Test, 31> const tests{
        PropertyStorageAndValidation, PropertyMetadataOutlivesRegistrationHandle,
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
        PanelRejectsWrongThreadMutations, DetachingAnAncestorCancelsNestedInput
    };
    for (auto const test : tests) { test(); }
    return static_cast<int>(tests.size());
}
