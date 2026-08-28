#include "ui/clipboard.h"

#include <wil/resource.h>

#include <cstring>
#include <limits>

namespace hlaunch::ui {

bool copyUnicodeTextToClipboard(
    const HWND owner,
    const std::wstring_view text) noexcept
{
    if (text.size() > (std::numeric_limits<std::size_t>::max() / sizeof(wchar_t)) - 1U
        || !OpenClipboard(owner)) {
        return false;
    }
    const auto clipboardCleanup = wil::scope_exit([] { CloseClipboard(); });
    if (!EmptyClipboard()) {
        return false;
    }

    const auto bytes = (text.size() + 1U) * sizeof(wchar_t);
    wil::unique_hglobal storage{GlobalAlloc(GMEM_MOVEABLE, bytes)};
    if (!storage) {
        return false;
    }
    void* destination = GlobalLock(storage.get());
    if (!destination) {
        return false;
    }
    std::memcpy(destination, text.data(), text.size() * sizeof(wchar_t));
    static_cast<wchar_t*>(destination)[text.size()] = L'\0';
    GlobalUnlock(storage.get());

    if (!SetClipboardData(CF_UNICODETEXT, storage.get())) {
        return false;
    }
    storage.release();
    return true;
}

} // namespace hlaunch::ui
