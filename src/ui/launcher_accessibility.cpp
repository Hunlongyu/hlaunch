#include "ui/launcher_accessibility.h"

#include <OleAuto.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>

namespace hlaunch::ui {
namespace {

constexpr std::wstring_view rootKey = L"root";

struct ProviderState final {
    explicit ProviderState(LauncherAccessibilityCallbacks value)
        : callbacks(std::move(value))
    {}
    LauncherAccessibilityCallbacks callbacks;
    std::atomic_bool connected{true};
    DWORD uiThreadId{GetCurrentThreadId()};
    std::mutex callbacksMutex;
    std::mutex snapshotMutex;
    std::vector<LauncherAccessibleNode> cachedSnapshot;
};

std::vector<LauncherAccessibleNode> snapshot(const std::shared_ptr<ProviderState>& state)
{
    if (!state->connected) return {};
    const std::scoped_lock lock{state->snapshotMutex};
    return state->cachedSnapshot;
}

void refreshSnapshot(const std::shared_ptr<ProviderState>& state)
{
    std::function<std::vector<LauncherAccessibleNode>()> callback{};
    {
        const std::scoped_lock lock{state->callbacksMutex};
        if (!state->connected || !state->callbacks.snapshot) return;
        callback = state->callbacks.snapshot;
    }
    auto updated = callback();
    const std::scoped_lock lock{state->snapshotMutex};
    if (!state->connected) return;
    state->cachedSnapshot = std::move(updated);
}

HWND providerWindow(const std::shared_ptr<ProviderState>& state)
{
    const std::scoped_lock lock{state->callbacksMutex};
    return state->connected ? state->callbacks.window : nullptr;
}

HRESULT dispatchAction(
    const std::shared_ptr<ProviderState>& state,
    const UINT message,
    const std::wstring_view key)
{
    std::function<void(std::wstring_view)> action{};
    HWND window{};
    {
        const std::scoped_lock lock{state->callbacksMutex};
        if (!state->connected) return UIA_E_ELEMENTNOTAVAILABLE;
        if (GetCurrentThreadId() == state->uiThreadId) {
            if (message == launcherAccessibilityInvokeMessage) {
                action = state->callbacks.invoke;
            }
            else if (message == launcherAccessibilityFocusMessage) {
                action = state->callbacks.focus;
            }
        }
        else {
            window = state->callbacks.window;
        }
    }
    if (GetCurrentThreadId() == state->uiThreadId) {
        if (action) action(key);
        return S_OK;
    }
    if (!window) return UIA_E_ELEMENTNOTAVAILABLE;
    std::wstring stableKey{key};
    SendMessageW(window, message, 0, reinterpret_cast<LPARAM>(&stableKey));
    return state->connected ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

const LauncherAccessibleNode* findNode(
    const std::vector<LauncherAccessibleNode>& nodes,
    const std::wstring_view key) noexcept
{
    const auto found = std::ranges::find(nodes, key, &LauncherAccessibleNode::key);
    return found == nodes.end() ? nullptr : &*found;
}

int runtimeHash(const std::wstring_view key) noexcept
{
    std::uint32_t hash = 2166136261U;
    for (const wchar_t value : key) {
        hash ^= static_cast<std::uint32_t>(value);
        hash *= 16777619U;
    }
    return static_cast<int>(hash & 0x7FFFFFFFU);
}

class Provider : public winrt::implements<
                           Provider,
                           IRawElementProviderSimple,
                           IRawElementProviderFragment,
                           IRawElementProviderFragmentRoot,
                           IInvokeProvider,
                           ISelectionProvider,
                           ISelectionItemProvider> {
public:
    Provider(std::shared_ptr<ProviderState> state, std::wstring key)
        : state_(std::move(state)), key_(std::move(key))
    {}

    HRESULT __stdcall get_ProviderOptions(ProviderOptions* options) noexcept final
    {
        if (!options) return E_POINTER;
        *options = ProviderOptions_ServerSideProvider;
        return S_OK;
    }

    HRESULT __stdcall GetPatternProvider(
        PATTERNID patternId,
        IUnknown** provider) noexcept final
    {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        try {
            const auto nodes = snapshot(state_);
            const auto node = findNode(nodes, key_);
            if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
            if (patternId == UIA_InvokePatternId && node->invokable) {
                return QueryInterface(IID_PPV_ARGS(provider));
            }
            if (patternId == UIA_SelectionPatternId && node->selectionContainer) {
                return QueryInterface(IID_PPV_ARGS(provider));
            }
            if (patternId == UIA_SelectionItemPatternId && node->selectable) {
                return QueryInterface(IID_PPV_ARGS(provider));
            }
            return S_OK;
        }
        catch (...) { return E_FAIL; }
    }

    HRESULT __stdcall GetPropertyValue(
        PROPERTYID propertyId,
        VARIANT* value) noexcept final
    {
        if (!value) return E_POINTER;
        VariantInit(value);
        try {
            const auto nodes = snapshot(state_);
            const auto node = findNode(nodes, key_);
            if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
            switch (propertyId) {
            case UIA_NamePropertyId:
                value->vt = VT_BSTR;
                value->bstrVal = SysAllocStringLen(node->name.data(), static_cast<UINT>(node->name.size()));
                return value->bstrVal || node->name.empty() ? S_OK : E_OUTOFMEMORY;
            case UIA_ControlTypePropertyId:
                value->vt = VT_I4;
                value->lVal = node->controlType;
                return S_OK;
            case UIA_IsKeyboardFocusablePropertyId:
                value->vt = VT_BOOL;
                value->boolVal = node->keyboardFocusable ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            case UIA_HasKeyboardFocusPropertyId:
                value->vt = VT_BOOL;
                value->boolVal = node->hasKeyboardFocus ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            case UIA_IsEnabledPropertyId:
                value->vt = VT_BOOL;
                value->boolVal = node->enabled ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            case UIA_IsOffscreenPropertyId:
                value->vt = VT_BOOL;
                value->boolVal = node->offscreen ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            case UIA_PositionInSetPropertyId:
                if (node->positionInSet > 0) { value->vt = VT_I4; value->lVal = node->positionInSet; }
                return S_OK;
            case UIA_SizeOfSetPropertyId:
                if (node->sizeOfSet > 0) { value->vt = VT_I4; value->lVal = node->sizeOfSet; }
                return S_OK;
            default:
                return S_OK;
            }
        }
        catch (...) { return E_FAIL; }
    }

    HRESULT __stdcall get_HostRawElementProvider(
        IRawElementProviderSimple** provider) noexcept final
    {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        try {
            const auto window = providerWindow(state_);
            return key_ == rootKey && window
                ? UiaHostProviderFromHwnd(window, provider)
                : S_OK;
        }
        catch (...) { return winrt::to_hresult(); }
    }

    HRESULT __stdcall Navigate(
        NavigateDirection direction,
        IRawElementProviderFragment** result) noexcept final
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        try {
            const auto nodes = snapshot(state_);
            const auto node = findNode(nodes, key_);
            if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
            std::wstring target{};
            if (direction == NavigateDirection_Parent) {
                target = node->parentKey;
            }
            else if (direction == NavigateDirection_FirstChild
                     || direction == NavigateDirection_LastChild) {
                std::vector<std::wstring> children{};
                for (const auto& candidate : nodes) {
                    if (candidate.parentKey == key_) children.push_back(candidate.key);
                }
                if (!children.empty()) target = direction == NavigateDirection_FirstChild
                    ? children.front() : children.back();
            }
            else {
                std::vector<std::wstring> siblings{};
                for (const auto& candidate : nodes) {
                    if (candidate.parentKey == node->parentKey) siblings.push_back(candidate.key);
                }
                const auto current = std::ranges::find(siblings, key_);
                if (current != siblings.end()) {
                    if (direction == NavigateDirection_NextSibling && std::next(current) != siblings.end()) {
                        target = *std::next(current);
                    }
                    else if (direction == NavigateDirection_PreviousSibling && current != siblings.begin()) {
                        target = *std::prev(current);
                    }
                }
            }
            if (!target.empty()) *result = makeFragment(target);
            return S_OK;
        }
        catch (...) { return E_FAIL; }
    }

    HRESULT __stdcall GetRuntimeId(SAFEARRAY** runtimeId) noexcept final
    {
        if (!runtimeId) return E_POINTER;
        *runtimeId = nullptr;
        if (key_ == rootKey) return S_OK;
        auto array = SafeArrayCreateVector(VT_I4, 0, 3);
        if (!array) return E_OUTOFMEMORY;
        LONG* values{};
        if (FAILED(SafeArrayAccessData(array, reinterpret_cast<void**>(&values)))) {
            SafeArrayDestroy(array);
            return E_FAIL;
        }
        values[0] = UiaAppendRuntimeId;
        values[1] = 1;
        values[2] = runtimeHash(key_);
        SafeArrayUnaccessData(array);
        *runtimeId = array;
        return S_OK;
    }

    HRESULT __stdcall get_BoundingRectangle(UiaRect* rectangle) noexcept final
    {
        if (!rectangle) return E_POINTER;
        try {
            const auto nodes = snapshot(state_);
            const auto node = findNode(nodes, key_);
            if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
            *rectangle = node->bounds;
            return S_OK;
        }
        catch (...) { return winrt::to_hresult(); }
    }

    HRESULT __stdcall GetEmbeddedFragmentRoots(SAFEARRAY** roots) noexcept final
    {
        if (!roots) return E_POINTER;
        *roots = nullptr;
        return S_OK;
    }

    HRESULT __stdcall SetFocus() noexcept final
    {
        try {
            return dispatchAction(state_, launcherAccessibilityFocusMessage, key_);
        }
        catch (...) { return E_FAIL; }
    }

    HRESULT __stdcall get_FragmentRoot(
        IRawElementProviderFragmentRoot** root) noexcept final
    {
        if (!root) return E_POINTER;
        *root = nullptr;
        try {
            *root = makeRoot();
            return *root ? S_OK : E_OUTOFMEMORY;
        }
        catch (...) { return winrt::to_hresult(); }
    }

    HRESULT __stdcall ElementProviderFromPoint(
        double x,
        double y,
        IRawElementProviderFragment** result) noexcept final
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        try {
            const auto nodes = snapshot(state_);
            for (auto current = nodes.rbegin(); current != nodes.rend(); ++current) {
                const auto& bounds = current->bounds;
                if (x >= bounds.left && x < bounds.left + bounds.width
                    && y >= bounds.top && y < bounds.top + bounds.height) {
                    *result = makeFragment(current->key);
                    break;
                }
            }
            return S_OK;
        }
        catch (...) { return winrt::to_hresult(); }
    }

    HRESULT __stdcall GetFocus(IRawElementProviderFragment** result) noexcept final
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        try {
            const auto nodes = snapshot(state_);
            const auto focused = std::ranges::find(
                nodes, true, &LauncherAccessibleNode::hasKeyboardFocus);
            if (focused != nodes.end()) *result = makeFragment(focused->key);
            return S_OK;
        }
        catch (...) { return winrt::to_hresult(); }
    }

    HRESULT __stdcall Invoke() noexcept final
    {
        try {
            return dispatchAction(state_, launcherAccessibilityInvokeMessage, key_);
        }
        catch (...) { return E_FAIL; }
    }

    HRESULT __stdcall GetSelection(SAFEARRAY** result) noexcept final
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        try {
            const auto nodes = snapshot(state_);
            std::vector<std::wstring> selected{};
            for (const auto& node : nodes) {
                if (node.parentKey == key_ && node.selected) selected.push_back(node.key);
            }
            auto array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(selected.size()));
            if (!array) return E_OUTOFMEMORY;
            for (LONG index = 0; index < static_cast<LONG>(selected.size()); ++index) {
                auto provider = makeFragment(selected[static_cast<std::size_t>(index)]);
                if (FAILED(SafeArrayPutElement(array, &index, provider))) {
                    if (provider) provider->Release();
                    SafeArrayDestroy(array);
                    return E_FAIL;
                }
                if (provider) provider->Release();
            }
            *result = array;
            return S_OK;
        }
        catch (...) { return E_FAIL; }
    }

    HRESULT __stdcall get_CanSelectMultiple(BOOL* value) noexcept final
    { if (!value) return E_POINTER; *value = FALSE; return S_OK; }
    HRESULT __stdcall get_IsSelectionRequired(BOOL* value) noexcept final
    { if (!value) return E_POINTER; *value = FALSE; return S_OK; }

    HRESULT __stdcall Select() noexcept final { return SetFocus(); }
    HRESULT __stdcall AddToSelection() noexcept final { return Select(); }
    HRESULT __stdcall RemoveFromSelection() noexcept final { return UIA_E_INVALIDOPERATION; }

    HRESULT __stdcall get_IsSelected(BOOL* value) noexcept final
    {
        if (!value) return E_POINTER;
        try {
            const auto nodes = snapshot(state_);
            const auto node = findNode(nodes, key_);
            if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
            *value = node->selected ? TRUE : FALSE;
            return S_OK;
        }
        catch (...) { return winrt::to_hresult(); }
    }

    HRESULT __stdcall get_SelectionContainer(
        IRawElementProviderSimple** container) noexcept final
    {
        if (!container) return E_POINTER;
        *container = nullptr;
        try {
            const auto nodes = snapshot(state_);
            const auto node = findNode(nodes, key_);
            if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
            auto provider = winrt::make_self<Provider>(state_, node->parentKey);
            *container = provider.as<IRawElementProviderSimple>().detach();
            return S_OK;
        }
        catch (...) { return winrt::to_hresult(); }
    }

private:
    IRawElementProviderFragment* makeFragment(const std::wstring_view key)
    {
        auto provider = winrt::make_self<Provider>(state_, std::wstring{key});
        return provider.as<IRawElementProviderFragment>().detach();
    }

    IRawElementProviderFragmentRoot* makeRoot()
    {
        auto provider = winrt::make_self<Provider>(state_, std::wstring{rootKey});
        return provider.as<IRawElementProviderFragmentRoot>().detach();
    }

    std::shared_ptr<ProviderState> state_;
    std::wstring key_;
};

} // namespace

struct LauncherAccessibility::Impl final {
    explicit Impl(LauncherAccessibilityCallbacks callbacks)
        : state(std::make_shared<ProviderState>(std::move(callbacks)))
    {
        refreshSnapshot(state);
        auto provider = winrt::make_self<Provider>(state, std::wstring{rootKey});
        root = provider.as<IRawElementProviderSimple>();
    }
    std::shared_ptr<ProviderState> state;
    winrt::com_ptr<IRawElementProviderSimple> root;
};

LauncherAccessibility::LauncherAccessibility(LauncherAccessibilityCallbacks callbacks)
    : impl_(std::make_unique<Impl>(std::move(callbacks)))
{}

LauncherAccessibility::~LauncherAccessibility() { disconnect(); }

LRESULT LauncherAccessibility::handleGetObject(const WPARAM wParam, const LPARAM lParam) noexcept
{
    try {
        if (!impl_ || !impl_->state->connected || lParam != UiaRootObjectId) return 0;
        refreshSnapshot(impl_->state);
        const auto window = providerWindow(impl_->state);
        return window
            ? UiaReturnRawElementProvider(window, wParam, lParam, impl_->root.get())
            : 0;
    }
    catch (...) {
        OutputDebugStringW(L"HLaunch UI Automation provider failed.\n");
        return 0;
    }
}

void LauncherAccessibility::raiseStructureChanged() noexcept
{
    try {
        if (!impl_ || !impl_->state->connected) return;
        refreshSnapshot(impl_->state);
        UiaRaiseStructureChangedEvent(
            impl_->root.get(), StructureChangeType_ChildrenInvalidated, nullptr, 0);
    }
    catch (...) {
        OutputDebugStringW(L"HLaunch UI Automation event failed.\n");
    }
}

void LauncherAccessibility::raiseFocusChanged(const std::wstring_view key) noexcept
{
    try {
        if (!impl_ || !impl_->state->connected) return;
        refreshSnapshot(impl_->state);
        auto provider = winrt::make_self<Provider>(impl_->state, std::wstring{key});
        UiaRaiseAutomationEvent(
            provider.as<IRawElementProviderSimple>().get(),
            UIA_AutomationFocusChangedEventId);
    }
    catch (...) {
        OutputDebugStringW(L"HLaunch UI Automation event failed.\n");
    }
}

void LauncherAccessibility::raiseSelectionChanged(const std::wstring_view key) noexcept
{
    try {
        if (!impl_ || !impl_->state->connected) return;
        refreshSnapshot(impl_->state);
        auto provider = winrt::make_self<Provider>(impl_->state, std::wstring{key});
        UiaRaiseAutomationEvent(
            provider.as<IRawElementProviderSimple>().get(),
            UIA_SelectionItem_ElementSelectedEventId);
    }
    catch (...) {
        OutputDebugStringW(L"HLaunch UI Automation event failed.\n");
    }
}

void LauncherAccessibility::refresh() noexcept
{
    try {
        if (impl_ && impl_->state->connected) refreshSnapshot(impl_->state);
    }
    catch (...) {
        OutputDebugStringW(L"HLaunch UI Automation refresh failed.\n");
    }
}

void LauncherAccessibility::disconnect() noexcept
{
    if (!impl_) return;
    {
        const std::scoped_lock lock{impl_->state->callbacksMutex};
        impl_->state->connected = false;
        impl_->state->callbacks.window = nullptr;
        impl_->state->callbacks.snapshot = {};
        impl_->state->callbacks.invoke = {};
        impl_->state->callbacks.focus = {};
    }
    {
        const std::scoped_lock lock{impl_->state->snapshotMutex};
        impl_->state->cachedSnapshot.clear();
    }
    impl_->root = nullptr;
}

} // namespace hlaunch::ui
