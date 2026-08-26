#include "platform/windows/drop_target.h"

#include <Ole2.h>
#include <shellapi.h>
#include <wil/resource.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <optional>
#include <utility>

namespace hlaunch::platform::windows {
namespace {

constexpr wchar_t internetUrlWideFormat[] = L"UniformResourceLocatorW";
constexpr wchar_t internetUrlAnsiFormat[] = L"UniformResourceLocator";

FORMATETC globalFormat(const CLIPFORMAT format) noexcept
{
    return FORMATETC{
        .cfFormat = format,
        .ptd = nullptr,
        .dwAspect = DVASPECT_CONTENT,
        .lindex = -1,
        .tymed = TYMED_HGLOBAL,
    };
}

bool supportsFormat(IDataObject *dataObject, const CLIPFORMAT format) noexcept
{
    auto descriptor = globalFormat(format);
    return dataObject && SUCCEEDED(dataObject->QueryGetData(&descriptor));
}

std::wstring trimText(std::wstring value)
{
    const auto isSpace = [](const wchar_t character) { return std::iswspace(character) != 0; };
    const auto first = std::ranges::find_if_not(value, isSpace);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    if (first >= last)
    {
        return {};
    }
    return std::wstring{first, last};
}

std::optional<std::wstring> readWideText(IDataObject *dataObject, const CLIPFORMAT format)
{
    auto descriptor = globalFormat(format);
    wil::unique_stg_medium medium{};
    if (!dataObject || FAILED(dataObject->GetData(&descriptor, medium.addressof())))
    {
        return std::nullopt;
    }
    if (medium.tymed != TYMED_HGLOBAL || !medium.hGlobal)
    {
        return std::nullopt;
    }
    wil::unique_hglobal_locked locked{medium.hGlobal};
    const auto *text = static_cast<const wchar_t *>(locked.get());
    if (!text)
    {
        return std::nullopt;
    }
    const auto capacity = GlobalSize(medium.hGlobal) / sizeof(wchar_t);
    const auto length = wcsnlen_s(text, capacity);
    return trimText(std::wstring{text, length});
}

std::optional<std::wstring> readAnsiText(IDataObject *dataObject, const CLIPFORMAT format)
{
    auto descriptor = globalFormat(format);
    wil::unique_stg_medium medium{};
    if (!dataObject || FAILED(dataObject->GetData(&descriptor, medium.addressof())))
    {
        return std::nullopt;
    }
    if (medium.tymed != TYMED_HGLOBAL || !medium.hGlobal)
    {
        return std::nullopt;
    }
    wil::unique_hglobal_locked locked{medium.hGlobal};
    const auto *text = static_cast<const char *>(locked.get());
    if (!text)
    {
        return std::nullopt;
    }
    const auto capacity = GlobalSize(medium.hGlobal);
    const auto length = strnlen_s(text, capacity);
    if (length == 0)
    {
        return std::wstring{};
    }
    const int wideLength = MultiByteToWideChar(
        CP_ACP, 0, text, static_cast<int>(length), nullptr, 0);
    if (wideLength <= 0)
    {
        return std::nullopt;
    }
    std::wstring value(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, text, static_cast<int>(length),
                            value.data(), wideLength) != wideLength)
    {
        return std::nullopt;
    }
    return trimText(std::move(value));
}

std::vector<DroppedSource> extractDroppedSources(IDataObject *dataObject)
{
    std::vector<DroppedSource> sources{};
    auto fileDescriptor = globalFormat(CF_HDROP);
    wil::unique_stg_medium fileMedium{};
    if (dataObject && SUCCEEDED(dataObject->GetData(&fileDescriptor, fileMedium.addressof())))
    {
        if (fileMedium.tymed == TYMED_HGLOBAL && fileMedium.hGlobal)
        {
            const auto drop = reinterpret_cast<HDROP>(fileMedium.hGlobal); // NOLINT(performance-no-int-to-ptr): HDROP is the CF_HDROP HGLOBAL handle.
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0);
            sources.reserve(count + 1U);
            for (UINT index = 0; index < count; ++index)
            {
                const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                std::wstring path(static_cast<std::size_t>(length) + 1U, L'\0');
                const UINT copied = DragQueryFileW(drop, index, path.data(), length + 1U);
                path.resize(copied);
                if (!path.empty())
                {
                    sources.push_back({DroppedSourceKind::Path, std::move(path)});
                }
            }
        }
    }

    if (sources.empty())
    {
        const auto urlWide =
            static_cast<CLIPFORMAT>(RegisterClipboardFormatW(internetUrlWideFormat));
        const auto urlAnsi =
            static_cast<CLIPFORMAT>(RegisterClipboardFormatW(internetUrlAnsiFormat));
        std::optional<std::wstring> url{};
        if (urlWide != 0)
        {
            url = readWideText(dataObject, urlWide);
        }
        if ((!url || url->empty()) && urlAnsi != 0)
        {
            url = readAnsiText(dataObject, urlAnsi);
        }
        if (!url || url->empty())
        {
            url = readWideText(dataObject, CF_UNICODETEXT);
        }
        if (url && !url->empty())
        {
            sources.push_back({DroppedSourceKind::Url, std::move(*url)});
        }
    }
    return sources;
}

class OleDropTarget : public winrt::implements<OleDropTarget, IDropTarget>
{
  public:
    explicit OleDropTarget(DropTarget::DropHandler handler) : handler_(std::move(handler)) {}

    HRESULT __stdcall DragEnter(IDataObject *dataObject, DWORD, POINTL,
                                DWORD *effect) noexcept final
    {
        const auto urlWide =
            static_cast<CLIPFORMAT>(RegisterClipboardFormatW(internetUrlWideFormat));
        const auto urlAnsi =
            static_cast<CLIPFORMAT>(RegisterClipboardFormatW(internetUrlAnsiFormat));
        canDrop_ = supportsFormat(dataObject, CF_HDROP) || supportsFormat(dataObject, CF_UNICODETEXT) ||
                   (urlWide != 0 && supportsFormat(dataObject, urlWide)) ||
                   (urlAnsi != 0 && supportsFormat(dataObject, urlAnsi));
        setEffect(effect);
        return S_OK;
    }

    HRESULT __stdcall DragOver(DWORD, POINTL, DWORD *effect) noexcept final
    {
        setEffect(effect);
        return S_OK;
    }

    HRESULT __stdcall DragLeave() noexcept final
    {
        canDrop_ = false;
        return S_OK;
    }

    HRESULT __stdcall Drop(IDataObject *dataObject, DWORD, POINTL point,
                           DWORD *effect) noexcept final
    {
        try
        {
            auto sources = extractDroppedSources(dataObject);
            canDrop_ = !sources.empty();
            setEffect(effect);
            if (canDrop_ && handler_)
            {
                handler_(std::move(sources), point);
            }
            canDrop_ = false;
            return S_OK;
        }
        catch (...)
        {
            canDrop_ = false;
            if (effect)
            {
                *effect = DROPEFFECT_NONE;
            }
            return E_FAIL;
        }
    }

  private:
    void setEffect(DWORD *effect) const noexcept
    {
        if (!effect)
        {
            return;
        }
        *effect = canDrop_ && ((*effect & DROPEFFECT_COPY) != 0) ? DROPEFFECT_COPY
                                                                 : DROPEFFECT_NONE;
    }

    DropTarget::DropHandler handler_{};
    bool canDrop_{};
};

} // namespace

winrt::com_ptr<IDropTarget> makeOleDropTarget(DropTarget::DropHandler handler)
{
    if (!handler)
    {
        return {};
    }
    return winrt::make_self<OleDropTarget>(std::move(handler)).as<IDropTarget>();
}

DropTarget::~DropTarget()
{
    revoke();
}

bool DropTarget::registerForWindow(const HWND window, DropHandler handler)
{
    revoke();
    if (!window || !handler)
    {
        return false;
    }
    implementation_ = makeOleDropTarget(std::move(handler));
    const HRESULT result = RegisterDragDrop(window, implementation_.get());
    if (FAILED(result))
    {
        implementation_ = nullptr;
        return false;
    }
    window_ = window;
    return true;
}

void DropTarget::revoke() noexcept
{
    if (window_)
    {
        RevokeDragDrop(window_);
        window_ = nullptr;
    }
    implementation_ = nullptr;
}

} // namespace hlaunch::platform::windows
