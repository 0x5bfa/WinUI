namespace Microsoft.UI.Xaml
{
    public sealed class DependencyProperty { }

    public class DependencyObject
    {
        public virtual long RegisterPropertyChangedCallback(DependencyProperty property,
            Action<DependencyObject, DependencyProperty> callback) => 0;
        public virtual void UnregisterPropertyChangedCallback(DependencyProperty property, long token) { }
    }

    public class FrameworkElement : DependencyObject
    {
        public event EventHandler? Loaded;
        public event EventHandler? Unloaded;
        public void RaiseLoaded() => Loaded?.Invoke(this, EventArgs.Empty);
        public void RaiseUnloaded() => Unloaded?.Invoke(this, EventArgs.Empty);
    }

    public static class Application
    {
        public static void LoadComponent(object instance, Uri uri)
        {
            if (instance is not Markup.IComponentConnector connector) return;
            connector.Connect(1, instance);
            connector.Connect(2, new Controls.Grid());
            connector.Connect(3, new Controls.TextBlock());
            connector.Connect(4, new Controls.Button());
        }
    }
}

namespace Microsoft.UI.Xaml.Controls
{
    public class Page : FrameworkElement { }
    public class Grid : FrameworkElement { public static void SetRow(DependencyObject target, int value) { } }
    public class Button : FrameworkElement
    {
        public event EventHandler? Click;
        public void RaiseClick() => Click?.Invoke(this, EventArgs.Empty);
    }
    public class TextBlock : FrameworkElement
    {
        public static readonly DependencyProperty TextProperty = new();
        private readonly Dictionary<long, Action<DependencyObject, DependencyProperty>> _callbacks = [];
        private long _nextToken;
        private string _text = "";
        public int ActiveCallbackCount => _callbacks.Count;
        public string Text
        {
            get => _text;
            set
            {
                _text = value;
                foreach (var callback in _callbacks.Values.ToArray()) callback(this, TextProperty);
            }
        }

        public override long RegisterPropertyChangedCallback(DependencyProperty property,
            Action<DependencyObject, DependencyProperty> callback)
        {
            var token = ++_nextToken;
            _callbacks.Add(token, callback);
            return token;
        }
        public override void UnregisterPropertyChangedCallback(DependencyProperty property, long token) =>
            _callbacks.Remove(token);
    }
}

namespace Microsoft.UI.Xaml.Markup
{
    public interface IComponentConnector { void Connect(int connectionId, object target); }
}
