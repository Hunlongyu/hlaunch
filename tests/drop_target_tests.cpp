#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "platform/windows/drop_target.h"

#include <doctest/doctest.h>

#include <Ole2.h>
#include <ShlObj_core.h>
#include <shellapi.h>
#include <winrt/base.h>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

using hlaunch::platform::windows::DroppedSource;
using hlaunch::platform::windows::DroppedSourceKind;

class TestDataObject : public winrt::implements<TestDataObject, IDataObject>
{
  public:
    TestDataObject(std::vector<std::wstring> paths, std::wstring text)
        : paths_(std::move(paths)), text_(std::move(text))
    {
    }

    HRESULT __stdcall GetData(FORMATETC *format, STGMEDIUM *medium) noexcept final
    {
        if (!format || !medium)
        {
            return E_POINTER;
        }
        *medium = {};
        if (FAILED(QueryGetData(format)))
        {
            return DV_E_FORMATETC;
        }

        if (format->cfFormat == CF_HDROP)
        {
            return makeFileDropMedium(medium);
        }
        return makeTextMedium(medium);
    }

    HRESULT __stdcall GetDataHere(FORMATETC *, STGMEDIUM *) noexcept final
    {
        return DATA_E_FORMATETC;
    }

    HRESULT __stdcall QueryGetData(FORMATETC *format) noexcept final
    {
        if (!format)
        {
            return E_POINTER;
        }
        if ((format->tymed & TYMED_HGLOBAL) == 0 || format->dwAspect != DVASPECT_CONTENT ||
            format->lindex != -1)
        {
            return DV_E_FORMATETC;
        }
        if (format->cfFormat == CF_HDROP && !paths_.empty())
        {
            return S_OK;
        }
        if (format->cfFormat == CF_UNICODETEXT && !text_.empty())
        {
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT __stdcall GetCanonicalFormatEtc(FORMATETC *, FORMATETC *output) noexcept final
    {
        if (!output)
        {
            return E_POINTER;
        }
        output->ptd = nullptr;
        return E_NOTIMPL;
    }

    HRESULT __stdcall SetData(FORMATETC *, STGMEDIUM *, BOOL) noexcept final
    {
        return E_NOTIMPL;
    }

    HRESULT __stdcall EnumFormatEtc(DWORD, IEnumFORMATETC **) noexcept final
    {
        return E_NOTIMPL;
    }

    HRESULT __stdcall DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) noexcept final
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT __stdcall DUnadvise(DWORD) noexcept final
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT __stdcall EnumDAdvise(IEnumSTATDATA **) noexcept final
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

  private:
    HRESULT makeFileDropMedium(STGMEDIUM *medium) const noexcept
    {
        std::size_t characterCount = 1;
        for (const auto &path : paths_)
        {
            characterCount += path.size() + 1;
        }
        const std::size_t byteCount = sizeof(DROPFILES) + characterCount * sizeof(wchar_t);
        const HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, byteCount);
        if (!storage)
        {
            return E_OUTOFMEMORY;
        }

        void *locked = GlobalLock(storage);
        if (!locked)
        {
            GlobalFree(storage);
            return E_OUTOFMEMORY;
        }
        auto *header = static_cast<DROPFILES *>(locked);
        header->pFiles = sizeof(DROPFILES);
        header->fWide = TRUE;
        auto *cursor = reinterpret_cast<wchar_t *>(
            static_cast<unsigned char *>(locked) + sizeof(DROPFILES));
        for (const auto &path : paths_)
        {
            std::memcpy(cursor, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
            cursor += path.size() + 1;
        }
        *cursor = L'\0';
        GlobalUnlock(storage);

        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = storage;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    HRESULT makeTextMedium(STGMEDIUM *medium) const noexcept
    {
        const std::size_t byteCount = (text_.size() + 1) * sizeof(wchar_t);
        const HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE, byteCount);
        if (!storage)
        {
            return E_OUTOFMEMORY;
        }
        void *locked = GlobalLock(storage);
        if (!locked)
        {
            GlobalFree(storage);
            return E_OUTOFMEMORY;
        }
        std::memcpy(locked, text_.c_str(), byteCount);
        GlobalUnlock(storage);

        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = storage;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    std::vector<std::wstring> paths_{};
    std::wstring text_{};
};

winrt::com_ptr<IDataObject> makeDataObject(
    std::vector<std::wstring> paths = {}, std::wstring text = {})
{
    return winrt::make_self<TestDataObject>(std::move(paths), std::move(text)).as<IDataObject>();
}

} // namespace

TEST_CASE("PROD-DROP-001 OLE target preserves CF_HDROP order and point")
{
    std::vector<DroppedSource> received{};
    POINTL receivedPoint{};
    auto target = hlaunch::platform::windows::makeOleDropTarget(
        [&](std::vector<DroppedSource> sources, const POINTL point) {
            received = std::move(sources);
            receivedPoint = point;
        });
    auto data = makeDataObject(
        {LR"(C:\Apps\first.exe)", LR"(C:\资料\second.txt)"}, L"https://ignored.example");

    DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    CHECK(target->DragEnter(data.get(), MK_LBUTTON, {}, &effect) == S_OK);
    CHECK(effect == DROPEFFECT_COPY);

    effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    const POINTL point{123, 456};
    CHECK(target->Drop(data.get(), MK_LBUTTON, point, &effect) == S_OK);
    CHECK(effect == DROPEFFECT_COPY);
    REQUIRE(received.size() == 2);
    CHECK(received[0].kind == DroppedSourceKind::Path);
    CHECK(received[0].value == LR"(C:\Apps\first.exe)");
    CHECK(received[1].kind == DroppedSourceKind::Path);
    CHECK(received[1].value == LR"(C:\资料\second.txt)");
    CHECK(receivedPoint.x == point.x);
    CHECK(receivedPoint.y == point.y);
}

TEST_CASE("PROD-DROP-001 OLE target accepts and trims Unicode URL text")
{
    std::vector<DroppedSource> received{};
    auto target = hlaunch::platform::windows::makeOleDropTarget(
        [&](std::vector<DroppedSource> sources, POINTL) { received = std::move(sources); });
    auto data = makeDataObject({}, L" \r\nhttps://example.com/path\t ");

    DWORD effect = DROPEFFECT_COPY;
    CHECK(target->DragEnter(data.get(), MK_LBUTTON, {}, &effect) == S_OK);
    CHECK(effect == DROPEFFECT_COPY);
    CHECK(target->Drop(data.get(), MK_LBUTTON, {}, &effect) == S_OK);
    REQUIRE(received.size() == 1);
    CHECK(received[0].kind == DroppedSourceKind::Url);
    CHECK(received[0].value == L"https://example.com/path");
}

TEST_CASE("PROD-DROP-001 OLE target rejects unsupported data")
{
    bool called = false;
    auto target = hlaunch::platform::windows::makeOleDropTarget(
        [&](std::vector<DroppedSource>, POINTL) { called = true; });
    auto data = makeDataObject();

    DWORD effect = DROPEFFECT_COPY;
    CHECK(target->DragEnter(data.get(), MK_LBUTTON, {}, &effect) == S_OK);
    CHECK(effect == DROPEFFECT_NONE);
    CHECK(target->Drop(data.get(), MK_LBUTTON, {}, &effect) == S_OK);
    CHECK(effect == DROPEFFECT_NONE);
    CHECK_FALSE(called);
}
