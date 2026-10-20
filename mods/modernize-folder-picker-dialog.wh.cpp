// ==WindhawkMod==
// @id              modernize-folder-picker-dialog
// @name            Modernize Folder Picker Dialog
// @description     Replaces the classic "Browse For Folder" dialog
// @version         1.0.0-shihab.1
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

#define RETURN_IF_FAILED(hr) if (FAILED(hr)) return SHBrowseForFolderW_orig(lpbi)

#define E_CANCELLED (HRESULT)0x800704C7

#include <initguid.h>
#include <shlobj.h>
#include <strsafe.h>
#include <winrt/base.h>
#include <windhawk_utils.h>

#include <atomic>

std::atomic<int> g_hookCallsInProgress = 0;

using SHBrowseForFolderW_t = decltype(&SHBrowseForFolderW);
SHBrowseForFolderW_t SHBrowseForFolderW_orig = nullptr;

static PIDLIST_ABSOLUTE ModernBrowseForFolder(LPBROWSEINFOW lpbi)
{
    /* The modern dialog needs a single-threaded apartment */
    APTTYPE aptType;
    APTTYPEQUALIFIER aptQualifier;
    if (FAILED(CoGetApartmentType(&aptType, &aptQualifier)) ||
        (aptType != APTTYPE_STA && aptType != APTTYPE_MAINSTA))
    {
        return SHBrowseForFolderW_orig(lpbi);
    }

    /* Requests the modern folder picker cannot serve */
    if (lpbi->ulFlags & (BIF_BROWSEINCLUDEFILES | BIF_BROWSEFORCOMPUTER |
                         BIF_BROWSEFORPRINTER))
    {
        return SHBrowseForFolderW_orig(lpbi);
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
    if (lpbi->ulFlags & BIF_RETURNONLYFSDIRS)
    {
        options |= FOS_FORCEFILESYSTEM;
    }
    RETURN_IF_FAILED(pDialog->SetOptions(options));

    if (lpbi->lpszTitle && *lpbi->lpszTitle)
    {
        RETURN_IF_FAILED(pDialog->SetTitle(lpbi->lpszTitle));
    }

    /* Tell it where to start */
    if (lpbi->pidlRoot)
    {
        /* We have to convert from PIDLIST_ABSOLUTE -> IShellItem * */
        winrt::com_ptr<IShellItem> pRoot = nullptr;
        RETURN_IF_FAILED(SHCreateItemFromIDList(
            lpbi->pidlRoot,
            IID_PPV_ARGS(&pRoot)
        ));
        RETURN_IF_FAILED(pDialog->SetFolder(pRoot.get()));
    }

    HRESULT hr = pDialog->Show(lpbi->hwndOwner);
    if (FAILED(hr))
    {
        /* For whatever reason, the dialog being cancelled counts as a fail */
        if (hr != E_CANCELLED)
        {
            return SHBrowseForFolderW_orig(lpbi);
        }
        else
        {
            return NULL;
        }
    }

    /* The user has chosen; from here on, never open a second dialog */
    winrt::com_ptr<IShellItem> pResult = nullptr;
    if (FAILED(pDialog->GetResult(pResult.put())) || !pResult)
    {
        return NULL;
    }

    PIDLIST_ABSOLUTE result = nullptr;
    if (FAILED(SHGetIDListFromObject(pResult.get(), &result)))
    {
        return NULL;
    }

    if (lpbi->pszDisplayName)
    {
        LPWSTR pszName = nullptr;
        if (SUCCEEDED(pResult->GetDisplayName(SIGDN_NORMALDISPLAY, &pszName)) &&
            pszName)
        {
            /* Documented as a MAX_PATH buffer; a longer name is cut short */
            StringCchCopyW(lpbi->pszDisplayName, MAX_PATH, pszName);
            CoTaskMemFree(pszName);
        }
        else
        {
            lpbi->pszDisplayName[0] = L'\0';
        }
    }

    return result;
}

PIDLIST_ABSOLUTE WINAPI SHBrowseForFolderW_hook(
    LPBROWSEINFOW lpbi
)
{
    if (!lpbi) return NULL;

    g_hookCallsInProgress++;
    PIDLIST_ABSOLUTE result = ModernBrowseForFolder(lpbi);
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
