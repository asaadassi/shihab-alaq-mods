// ==WindhawkMod==
// @id              modernize-folder-picker-dialog
// @name            Modernize Folder Picker Dialog
// @name:ar         نافذة اختيار مجلد حديثة
// @description     Replaces the classic "Browse For Folder" dialog
// @description:ar  يستبدل نافذة «استعراض بحثاً عن مجلد» القديمة بنافذة الاختيار الحديثة
// @version         1.0.0-shihab.4
// @author          aubymori
// @github          https://github.com/aubymori
// @include         *
// @compilerOptions -lole32 -loleaut32
// @license         MIT
// ==/WindhawkMod==

// Adapted for Shihab Alaq (based on aubymori's original 1.0.0, MIT):
// - Only runs the modern dialog on a COM single-threaded apartment thread;
//   other threads get the original dialog (the modern one needs STA).
// - Requests the dialog cannot serve (include files, pick a computer or a
//   printer) go to the original dialog instead of returning a folder.
// - "Only file system folders" is passed on to the modern dialog.
// - Once the user has chosen, the original dialog is no longer opened a second
//   time when reading the result fails; the result is taken as an ID list
//   directly, so non file system folders no longer fail.
// - Fills pszDisplayName (at most MAX_PATH characters) as callers expect,
//   instead of leaving their buffer unset.
// - Counts calls in progress and waits for them in Wh_ModUninit, so the mod is
//   not unloaded while a dialog it opened is still on screen.
// - Opens where the program asks: most programs choose the start folder from
//   their callback (BFFM_SETSELECTION on BFFM_INITIALIZED), which the modern
//   dialog never called, so it always opened on Documents or Desktop.
// - Hooks SHBrowseForFolderA too, so older ANSI programs get the modern dialog.

// ==WindhawkModReadme==
/*
# Modernize Folder Picker Dialog

This mod replaces the outdated, hard-to-use "Browse For Folder" dialog with a
modern styled folder picker dialog, which is easier to navigate.

**Before**:

![Before](https://raw.githubusercontent.com/aubymori/images/main/modernize-folder-picker-dialog-before.png)

**After**:

![After](https://raw.githubusercontent.com/aubymori/images/main/modernize-folder-picker-dialog-after.png)
*/
// ==/WindhawkModReadme==

#define RETURN_IF_FAILED(hr) if (FAILED(hr)) return BrowseOutcome::UseOriginal

#define E_CANCELLED (HRESULT)0x800704C7

#include <initguid.h>
#include <shlobj.h>
#include <strsafe.h>
#include <winrt/base.h>
#include <windhawk_utils.h>

#include <atomic>
#include <string>

std::atomic<int> g_hookCallsInProgress = 0;

using SHBrowseForFolderW_t = decltype(&SHBrowseForFolderW);
SHBrowseForFolderW_t SHBrowseForFolderW_orig = nullptr;

using SHBrowseForFolderA_t = decltype(&SHBrowseForFolderA);
SHBrowseForFolderA_t SHBrowseForFolderA_orig = nullptr;

/* What both the W and the A calls ask for, with the title already wide */
struct BrowseRequest
{
    HWND hwndOwner;
    PCIDLIST_ABSOLUTE pidlRoot;
    LPCWSTR lpszTitle;
    UINT ulFlags;
    BFFCALLBACK lpfn;
    LPARAM lParam;
};

enum class BrowseOutcome { Handled, UseOriginal };

static std::wstring AnsiToWide(LPCSTR text)
{
    int length = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(length, L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), length);
    wide.resize(length - 1);
    return wide;
}

/* Keeps the folder the program selects while its callback runs */
static LRESULT CALLBACK SelectionWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* selection = reinterpret_cast<winrt::com_ptr<IShellItem>*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case BFFM_SETSELECTIONA:
    case BFFM_SETSELECTIONW:
    case BFFM_SETEXPANDED:
    {
        /* wParam TRUE: a path (ANSI for BFFM_SETSELECTIONA), FALSE: an ID list */
        winrt::com_ptr<IShellItem> item;
        if (lParam && !wParam)
        {
            SHCreateItemFromIDList(reinterpret_cast<PCIDLIST_ABSOLUTE>(lParam),
                                   IID_PPV_ARGS(item.put()));
        }
        else if (lParam && msg == BFFM_SETSELECTIONA)
        {
            std::wstring path = AnsiToWide(reinterpret_cast<LPCSTR>(lParam));
            SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item.put()));
        }
        else if (lParam)
        {
            SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(lParam), nullptr,
                                        IID_PPV_ARGS(item.put()));
        }
        if (item && selection)
        {
            *selection = item;
        }
        return TRUE;
    }
    /* BFFM_ENABLEOK, BFFM_SETSTATUSTEXT, BFFM_SETOKTEXT have no place in the
       modern dialog */
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

/* Runs the program's BFFM_INITIALIZED callback against a hidden window and
   returns the folder it selected, if any */
static winrt::com_ptr<IShellItem> StartFolderFromCallback(const BrowseRequest& req)
{
    winrt::com_ptr<IShellItem> selection;
    if (!req.lpfn) return selection;

    HWND hwnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                NULL, NULL, NULL);
    if (!hwnd)
    {
        Wh_Log(L"Failed to create the callback window: %u", GetLastError());
        return selection;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&selection));
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SelectionWndProc));

    req.lpfn(hwnd, BFFM_INITIALIZED, 0, req.lParam);

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    DestroyWindow(hwnd);
    return selection;
}

/* Shows the modern dialog; UseOriginal when the classic one must be shown.
   displayName, when given, is a MAX_PATH buffer filled on a choice. */
static BrowseOutcome ModernBrowseForFolder(const BrowseRequest& req,
                                           PIDLIST_ABSOLUTE* result,
                                           LPWSTR displayName)
{
    *result = nullptr;

    /* The modern dialog needs a single-threaded apartment */
    APTTYPE aptType;
    APTTYPEQUALIFIER aptQualifier;
    if (FAILED(CoGetApartmentType(&aptType, &aptQualifier)) ||
        (aptType != APTTYPE_STA && aptType != APTTYPE_MAINSTA))
    {
        return BrowseOutcome::UseOriginal;
    }

    /* Requests the modern folder picker cannot serve */
    if (req.ulFlags & (BIF_BROWSEINCLUDEFILES | BIF_BROWSEFORCOMPUTER |
                       BIF_BROWSEFORPRINTER))
    {
        return BrowseOutcome::UseOriginal;
    }

    /* Make the dialog */
    winrt::com_ptr<IFileOpenDialog> pDialog = nullptr;
    RETURN_IF_FAILED(CoCreateInstance(
        CLSID_FileOpenDialog,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pDialog)
    ));

    /* Make it pick a folder instead of a file */
    FILEOPENDIALOGOPTIONS options =
        FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST;
    if (req.ulFlags & BIF_RETURNONLYFSDIRS)
    {
        options |= FOS_FORCEFILESYSTEM;
    }
    RETURN_IF_FAILED(pDialog->SetOptions(options));

    if (req.lpszTitle && *req.lpszTitle)
    {
        RETURN_IF_FAILED(pDialog->SetTitle(req.lpszTitle));
    }

    /* Tell it where to start */
    if (req.pidlRoot)
    {
        /* We have to convert from PIDLIST_ABSOLUTE -> IShellItem * */
        winrt::com_ptr<IShellItem> pRoot = nullptr;
        RETURN_IF_FAILED(SHCreateItemFromIDList(
            req.pidlRoot,
            IID_PPV_ARGS(&pRoot)
        ));
        RETURN_IF_FAILED(pDialog->SetFolder(pRoot.get()));
    }

    /* The folder the program selects in its callback wins over the root, as in
       the classic dialog. If Show fails below, the classic dialog runs the
       callback a second time, which it can do. */
    winrt::com_ptr<IShellItem> pStart = StartFolderFromCallback(req);
    if (pStart && FAILED(pDialog->SetFolder(pStart.get())))
    {
        Wh_Log(L"The program's start folder was refused");
    }

    HRESULT hr = pDialog->Show(req.hwndOwner);
    if (FAILED(hr))
    {
        /* For whatever reason, the dialog being cancelled counts as a fail */
        return hr == E_CANCELLED ? BrowseOutcome::Handled : BrowseOutcome::UseOriginal;
    }

    /* The user has chosen; from here on, never open a second dialog */
    winrt::com_ptr<IShellItem> pResult = nullptr;
    if (FAILED(pDialog->GetResult(pResult.put())) || !pResult)
    {
        return BrowseOutcome::Handled;
    }

    if (FAILED(SHGetIDListFromObject(pResult.get(), result)))
    {
        *result = nullptr;
        return BrowseOutcome::Handled;
    }

    if (displayName)
    {
        LPWSTR pszName = nullptr;
        if (SUCCEEDED(pResult->GetDisplayName(SIGDN_NORMALDISPLAY, &pszName)) &&
            pszName)
        {
            /* Documented as a MAX_PATH buffer; a longer name is cut short */
            StringCchCopyW(displayName, MAX_PATH, pszName);
            CoTaskMemFree(pszName);
        }
        else
        {
            displayName[0] = L'\0';
        }
    }

    return BrowseOutcome::Handled;
}

PIDLIST_ABSOLUTE WINAPI SHBrowseForFolderW_hook(
    LPBROWSEINFOW lpbi
)
{
    if (!lpbi) return NULL;

    g_hookCallsInProgress++;
    BrowseRequest req = { lpbi->hwndOwner, lpbi->pidlRoot, lpbi->lpszTitle,
                          lpbi->ulFlags, lpbi->lpfn, lpbi->lParam };
    PIDLIST_ABSOLUTE result = nullptr;
    if (ModernBrowseForFolder(req, &result, lpbi->pszDisplayName) ==
        BrowseOutcome::UseOriginal)
    {
        result = SHBrowseForFolderW_orig(lpbi);
    }
    g_hookCallsInProgress--;
    return result;
}

PIDLIST_ABSOLUTE WINAPI SHBrowseForFolderA_hook(
    LPBROWSEINFOA lpbi
)
{
    if (!lpbi) return NULL;

    g_hookCallsInProgress++;
    std::wstring title = lpbi->lpszTitle ? AnsiToWide(lpbi->lpszTitle) : std::wstring();
    BrowseRequest req = { lpbi->hwndOwner, lpbi->pidlRoot, title.c_str(),
                          lpbi->ulFlags, lpbi->lpfn, lpbi->lParam };
    PIDLIST_ABSOLUTE result = nullptr;
    WCHAR displayName[MAX_PATH] = L"";
    if (ModernBrowseForFolder(req, &result, lpbi->pszDisplayName ? displayName : nullptr) ==
        BrowseOutcome::UseOriginal)
    {
        result = SHBrowseForFolderA_orig(lpbi);
    }
    else if (result && lpbi->pszDisplayName)
    {
        /* Also a MAX_PATH buffer; a name that doesn't fit is left empty */
        if (!WideCharToMultiByte(CP_ACP, 0, displayName, -1, lpbi->pszDisplayName,
                                 MAX_PATH, NULL, NULL))
        {
            lpbi->pszDisplayName[0] = '\0';
        }
    }
    g_hookCallsInProgress--;
    return result;
}

BOOL Wh_ModInit(void)
{
    if (!WindhawkUtils::Wh_SetFunctionHookT(
        SHBrowseForFolderW,
        SHBrowseForFolderW_hook,
        &SHBrowseForFolderW_orig
    ))
    {
        Wh_Log(L"Failed to hook SHBrowseForFolderW");
        return FALSE;
    }

    /* Without it, ANSI programs keep the classic dialog; not a reason to fail */
    if (!WindhawkUtils::Wh_SetFunctionHookT(
        SHBrowseForFolderA,
        SHBrowseForFolderA_hook,
        &SHBrowseForFolderA_orig
    ))
    {
        Wh_Log(L"Failed to hook SHBrowseForFolderA");
    }

    return TRUE;
}

void Wh_ModUninit(void)
{
    /* A dialog opened by the hook may still be on screen; returning into an
       unloaded mod would crash the program, so wait until it is closed */
    while (g_hookCallsInProgress > 0)
    {
        Sleep(200);
    }
}
