#include "domain/Shop.h"
#include "session/SessionKeeper.h"
#include "app/ApplicationDiagnostics.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "support/CoreMath.h"
#include "app/ApplicationNetwork.h"
#include "I18N/All.h"
#include "session/SessionGameplay.h"
#include "render/Text.h"
#include "ui/features/Social/SocialLogic.h"
#include "render/Textures.h"
#include "domain/ItemsSkills.h"
#include "ui/session/UiSessionLogic.h"
#include "support/Camera.h"
#include "session/SessionUi.h"
#include "session/SessionRender.h"
#include "render/ModelResources.h"
#include "data/Localization.h"
#include "domain/CharacterSystem.h"
#include "data/ItemData.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"

#ifdef _WIN32
#include <strsafe.h>
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
#include <process.h>
#include <urlmon.h>
#pragma comment(lib, "Urlmon.lib")
#endif
#endif
// libcurl implementation of the shop file downloader (issue #462). Compiled
// only on non-Windows; on Windows the WinINet FileDownloader is used instead.

#if !defined(_WIN32) && defined(KJH_ADD_INGAMESHOP_UI_SYSTEM)

#include <curl/curl.h>

namespace
{
// Mirrors the WinINet downloader's fixed 3s connect timeout.
constexpr long kConnectTimeoutMs = 3000;
// Abort a transfer that stalls below 1 byte/s for this long, so a dead server
// cannot hang the shop indefinitely (the non-Windows path runs inline, without
// the Windows watchdog thread).
constexpr long kLowSpeedLimitBytesPerSec = 1;
constexpr long kLowSpeedTimeoutSec = 30;

// libcurl needs one process-wide init before any easy handle is created.
// curl_easy_init would do it lazily, but not thread-safely; do it once here.
void EnsureCurlGlobalInit()
{
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::string WideToUtf8(const std::wstring &value)
{
    if (value.empty())
    {
        return std::string();
    }

    // ConvertToUtf8 treats its third argument as the destination capacity and
    // clamps to it; size for the worst-case 4-byte UTF-8 expansion plus a null.
    std::vector<char> buffer(value.size() * 4 + 1, '\0');
    CMultiLanguage::ConvertToUtf8(buffer.data(), value.c_str(), static_cast<int>(buffer.size()));
    return std::string(buffer.data());
}

std::wstring Utf8ToWide(const char *value)
{
    if (value == nullptr || *value == '\0')
    {
        return std::wstring();
    }

    std::vector<wchar_t> buffer(std::strlen(value) + 1, L'\0');
    CMultiLanguage::ConvertFromUtf8(buffer.data(), value, static_cast<int>(buffer.size()));
    return std::wstring(buffer.data());
}

// Progress callback: a non-zero return aborts the transfer. Used to honour the
// caller's break flag.
int XferInfoCallback(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    const volatile int *pBreak = static_cast<const volatile int *>(clientp);
    return (pBreak != nullptr && *pBreak != 0) ? 1 : 0;
}

size_t WriteToStream(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *out = static_cast<std::ofstream *>(userdata);
    const size_t byteCount = size * nmemb;
    out->write(ptr, static_cast<std::streamsize>(byteCount));
    // A short return tells libcurl the write failed and aborts the transfer.
    return out->good() ? byteCount : 0;
}
} // namespace

WZResult CurlFileDownloader::DownloadFile(const std::wstring &url, const std::wstring &localPath,
                                          const std::wstring &username,
                                          const std::wstring &password, bool passiveFtp,
                                          const volatile int *pBreak)
{
    WZResult result;

    const std::filesystem::path targetPath(localPath);
    std::error_code fsError;
    if (targetPath.has_parent_path())
    {
        std::filesystem::create_directories(targetPath.parent_path(), fsError);
    }

    std::ofstream out(targetPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        result.SetResult(DL_CREATE_LOCALFILE, 0,
                         L"[CurlFileDownloader] Fail : cannot open local file %ls",
                         localPath.c_str());
        return result;
    }

    // Run the throwing UTF-8 conversions before curl_easy_init so no exception
    // can leak the CURL handle between init and curl_easy_cleanup.
    const std::string urlUtf8 = WideToUtf8(url);
    const std::string userUtf8 = WideToUtf8(username);
    const std::string passwordUtf8 = WideToUtf8(password);

    EnsureCurlGlobalInit();
    CURL *curl = curl_easy_init();
    if (curl == nullptr)
    {
        result.SetResult(DL_CREATE_SESSION, 0, L"[CurlFileDownloader] Fail : curl_easy_init");
        return result;
    }

    curl_easy_setopt(curl, CURLOPT_URL, urlUtf8.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &WriteToStream);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L); // HTTP >= 400 is a failure
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, kLowSpeedLimitBytesPerSec);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, kLowSpeedTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &XferInfoCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, const_cast<int *>(pBreak));
    curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, passiveFtp ? 1L : 0L);
    if (!passiveFtp)
    {
        curl_easy_setopt(curl, CURLOPT_FTPPORT, "-"); // active mode
    }
    if (!userUtf8.empty())
    {
        curl_easy_setopt(curl, CURLOPT_USERNAME, userUtf8.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, passwordUtf8.c_str());
    }

    const CURLcode code = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    out.close();

    if (code == CURLE_OK)
    {
        result.SetSuccessResult();
        return result;
    }

    // Remove the partial file so a failed download is not mistaken for a good one.
    std::filesystem::remove(targetPath, fsError);

    if (code == CURLE_ABORTED_BY_CALLBACK)
    {
        result.SetResult(WZ_USER_BREAK, code, L"[CurlFileDownloader] User Break : %ls",
                         url.c_str());
    }
    else
    {
        const std::wstring reason = Utf8ToWide(curl_easy_strerror(code));
        result.SetResult(DL_READ_REMOTEFILE, code, L"[CurlFileDownloader] Fail : %ls (%ls)",
                         reason.c_str(), url.c_str());
    }

    return result;
}

#endif // !_WIN32 && KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef _WIN32
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

DownloadFileInfo::DownloadFileInfo() // OK
{
    this->m_uFileLength = 0;
    RtlSecureZeroMemory(this->m_szFileName, sizeof(this->m_szFileName));
    RtlSecureZeroMemory(this->m_szLocalFilePath, sizeof(this->m_szLocalFilePath));
    RtlSecureZeroMemory(this->m_szRemoteFilePath, sizeof(this->m_szRemoteFilePath));
    RtlSecureZeroMemory(this->m_szTargerDirPath, sizeof(this->m_szTargerDirPath));
}

DownloadFileInfo::~DownloadFileInfo() // OK
{
}

TCHAR *DownloadFileInfo::GetFileName() // OK
{
    return m_szFileName;
}

TCHAR *DownloadFileInfo::GetLocalFilePath() // OK
{
    return m_szLocalFilePath;
}

TCHAR *DownloadFileInfo::GetRemoteFilePath() // OK
{
    return m_szRemoteFilePath;
}

TCHAR *DownloadFileInfo::GetTargetDirPath() // OK
{
    return m_szTargerDirPath;
}

ULONGLONG DownloadFileInfo::GetFileLength() // OK
{
    return this->m_uFileLength;
}

void DownloadFileInfo::SetFilePath(TCHAR *szFileName, TCHAR *szLocalFilePath,
                                   TCHAR *szRemoteFilePath, TCHAR *szTargerDirPath) // OK
{
    StringCchCopy(this->m_szFileName, sizeof(this->m_szFileName), szFileName);
    StringCchCopy(this->m_szLocalFilePath, sizeof(this->m_szLocalFilePath), szLocalFilePath);
    StringCchCopy(this->m_szRemoteFilePath, sizeof(this->m_szRemoteFilePath), szRemoteFilePath);
    if (szTargerDirPath)
        StringCchCopy(this->m_szTargerDirPath, sizeof(this->m_szTargerDirPath), szTargerDirPath);
}

void DownloadFileInfo::SetFileLength(ULONGLONG uFileLength) // OK
{
    this->m_uFileLength = uFileLength;
}

DownloadServerInfo::DownloadServerInfo() // OK
{
    this->m_nPort = 21;
    this->m_DownloaderType = FTP;
    this->m_dwReadBufferSize = DL_DEFAULT_BUFFER_SIZE;
    this->m_bOverWrite = 1;
    this->m_bPassive = 0;
    this->m_dwConnectTimeout = 0;
    RtlSecureZeroMemory(this->m_szServerURL, sizeof(this->m_szServerURL));
    RtlSecureZeroMemory(this->m_szUserID, sizeof(this->m_szUserID));
    RtlSecureZeroMemory(this->m_szPassword, sizeof(this->m_szPassword));
}

DownloadServerInfo::~DownloadServerInfo() // OK
{
}

DWORD DownloadServerInfo::GetConnectTimeout() // OK
{
    return this->m_dwConnectTimeout;
}

DownloaderType DownloadServerInfo::GetDownloaderType() // OK
{
    return this->m_DownloaderType;
}

TCHAR *DownloadServerInfo::GetPassword() // OK
{
    return this->m_szPassword;
}

INTERNET_PORT DownloadServerInfo::GetPort() // OK
{
    return this->m_nPort;
}

DWORD DownloadServerInfo::GetReadBufferSize() // OK
{
    return this->m_dwReadBufferSize;
}

TCHAR *DownloadServerInfo::GetServerURL() // OK
{
    return this->m_szServerURL;
}

TCHAR *DownloadServerInfo::GetUserID() // OK
{
    return this->m_szUserID;
}

BOOL DownloadServerInfo::IsOverWrite() // OK
{
    return this->m_bOverWrite;
}

BOOL DownloadServerInfo::IsPassive() // OK
{
    return this->m_bPassive;
}

void DownloadServerInfo::SetServerInfo(TCHAR *szServerURL, INTERNET_PORT nPort, TCHAR *szUserID,
                                       TCHAR *szPassword) // OK
{
    auto *search = wcschr(szServerURL, ':');
    if (search && search[1] == '/' && search[2] == '/')
        StringCchCopy(this->m_szServerURL, sizeof(this->m_szServerURL), search + 3);
    else
        StringCchCopy(this->m_szServerURL, sizeof(this->m_szServerURL), szServerURL);
    this->m_nPort = nPort;
    StringCchCopy(this->m_szUserID, sizeof(this->m_szUserID), szUserID);
    StringCchCopy(this->m_szPassword, sizeof(this->m_szPassword), szPassword);
}

void DownloadServerInfo::SetDownloaderType(DownloaderType dwDownloaderType) // OK
{
    this->m_DownloaderType = dwDownloaderType;
}

void DownloadServerInfo::SetReadBufferSize(DWORD dwReadBufferSize) // OK
{
    this->m_dwReadBufferSize = dwReadBufferSize;
}

void DownloadServerInfo::SetOverWrite(BOOL bOverWrite) // OK
{
    this->m_bOverWrite = bOverWrite;
}

void DownloadServerInfo::SetPassiveMode(BOOL bPassive) // OK
{
    this->m_bPassive = bPassive;
}

void DownloadServerInfo::SetConnectTimeout(DWORD dwConnectTimeout) // OK
{
    this->m_dwConnectTimeout = dwConnectTimeout;
}
#endif

#endif // _WIN32

#ifdef _WIN32
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

FTPConnecter::FTPConnecter(DownloadServerInfo *pServerInfo, DownloadFileInfo *pFileInfo)
    : IConnecter(pServerInfo, pFileInfo)
{
}

FTPConnecter::~FTPConnecter()
{
}

WZResult FTPConnecter::CreateSession(HINTERNET &hSession)
{
    wchar_t path[MAX_PATH] = {0};

    Path::GetCurrentFileName(path);

    if ((hSession = InternetOpen(path, 0, 0, 0, 0)))
    {
        this->m_Result.SetSuccessResult();
    }
    else
    {
        this->m_Result.SetResult(
            DL_CREATE_SESSION, GetLastError(),
            L"[FTPConnecter::CreateSession] Fail : InternetOpen, FileName = %ls",
            this->m_pFileInfo->GetRemoteFilePath());
    }

    return this->m_Result;
}

WZResult FTPConnecter::CreateConnection(HINTERNET &hSession, HINTERNET &hConnection)
{
    if ((hConnection = InternetConnect(
             hSession, this->m_pServerInfo->GetServerURL(), this->m_pServerInfo->GetPort(),
             this->m_pServerInfo->GetUserID(), this->m_pServerInfo->GetPassword(), 1,
             this->m_pServerInfo->IsPassive() != 0 ? 0x8000000 : 0, (DWORD_PTR)this)))
    {
        this->m_Result.SetSuccessResult();
    }
    else
    {
        this->m_Result.SetResult(
            DL_CREATE_CONNECTION, GetLastError(),
            L"[FTPConnecter::CreateConnection] Fail : InternetConnect, FileName = %ls",
            this->m_pFileInfo->GetRemoteFilePath());
    }

    return this->m_Result;
}

WZResult FTPConnecter::OpenRemoteFile(HINTERNET &hConnection, HINTERNET &hRemoteFile,
                                      ULONGLONG &nFileLength)
{
    struct _WIN32_FIND_DATAW FindFileData = {0};

    HINTERNET hInternet = FtpFindFirstFile(hConnection, this->m_pFileInfo->GetRemoteFilePath(),
                                           &FindFileData, 0x84000000, (DWORD_PTR)this);

    if (hInternet)
    {
        nFileLength = (ULONGLONG)((ULONGLONG)FindFileData.nFileSizeHigh << (ULONGLONG)32) |
                      (ULONGLONG)(FindFileData.nFileSizeLow);

        InternetCloseHandle(hInternet);

        hRemoteFile = FtpOpenFile(hConnection, this->m_pFileInfo->GetRemoteFilePath(), 0x80000000,
                                  0x84000002, (DWORD_PTR)this);

        if (hRemoteFile)
        {
            this->m_Result.SetSuccessResult();
        }
        else
        {
            this->m_Result.SetResult(
                DL_OPEN_REMOTEFILE, GetLastError(),
                L"[FTPConnecter::OpenRemoteFile] Fail : FtpOpenFile, FileName = %ls",
                this->m_pFileInfo->GetRemoteFilePath());
        }
    }
    else
    {
        this->m_Result.SetResult(
            DL_GET_FILE_LENGTH, 0,
            L"[FTPConnecter::OpenRemoteFile] Fail : FtpFindFirstFile, FileName = %ls",
            this->m_pFileInfo->GetRemoteFilePath());
    }

    return this->m_Result;
}

WZResult FTPConnecter::ReadRemoteFile(HINTERNET &hRemoteFile, BYTE *byReadBuffer,
                                      DWORD *dwBytesRead)
{
    DWORD Size = this->m_pServerInfo->GetReadBufferSize();

    if (InternetReadFile(hRemoteFile, byReadBuffer, Size, dwBytesRead))
    {
        this->m_Result.SetSuccessResult();
    }
    else
    {
        this->m_Result.SetResult(
            DL_READ_REMOTEFILE, GetLastError(),
            L"[FTPConnecter::ReadRemoteFile] Fail : InternetReadFile, FileName = %ls",
            this->m_pFileInfo->GetRemoteFilePath());
    }

    return this->m_Result;
}
#endif

#endif // _WIN32

#ifdef _WIN32
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
FileDownloader::FileDownloader(IDownloaderStateEvent *pStateEvent, DownloadServerInfo *pServerInfo,
                               DownloadFileInfo *pFileInfo) // OK
{
    this->m_bBreak = 0;
    this->m_pStateEvent = pStateEvent;
    this->m_pServerInfo = pServerInfo;
    this->m_pFileInfo = pFileInfo;
    this->m_pConnecter = 0;
    this->m_hLocalFile = INVALID_HANDLE_VALUE;
    this->m_nFileLength = 0;
}

FileDownloader::~FileDownloader() // OK
{
    this->Release();
}

void FileDownloader::Break()
{
    this->m_bBreak = 1;
}

WZResult FileDownloader::DownloadFile() // OK
{
    this->m_bBreak = 0;
    this->m_nFileLength = 0;
    this->Release();
    this->m_pConnecter = this->CreateConnecter();
    this->m_Result = this->m_pConnecter->CreateSession(this->m_hSession);

    if (!this->CanBeContinue())
        goto JUMP_END;

    this->m_Result = this->CreateConnection();
    if (!this->CanBeContinue())
        goto JUMP_END;

    this->m_Result =
        this->m_pConnecter->OpenRemoteFile(m_hConnection, m_hRemoteFile, m_nFileLength);

    if (!this->CanBeContinue())
        goto JUMP_END;

    this->m_Result = this->CreateLocalFile();

    if (!this->CanBeContinue())
        goto JUMP_END;

    this->m_pFileInfo->SetFileLength(this->m_nFileLength);
    this->m_Result = this->TransferRemoteFile();

JUMP_END:
    return this->m_Result;
}

BOOL FileDownloader::CanBeContinue() // OK
{
    if (this->m_bBreak)
        this->m_Result.SetResult(WZ_USER_BREAK, WZ_SUCCESS, L"[FileDownloader] User Break");
    return this->m_Result.IsSuccess();
}

void FileDownloader::Release() // OK
{
    if (this->m_hLocalFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(this->m_hLocalFile);
        this->m_hLocalFile = INVALID_HANDLE_VALUE;
    }
    if (this->m_hRemoteFile)
    {
        InternetCloseHandle(this->m_hRemoteFile);
        this->m_hRemoteFile = 0;
    }
    if (this->m_hConnection)
    {
        InternetCloseHandle(this->m_hConnection);
        this->m_hConnection = 0;
    }
    if (this->m_hSession)
    {
        InternetCloseHandle(this->m_hSession);
        this->m_hSession = 0;
    }

    SAFE_DELETE(m_pConnecter);
}

IConnecter *FileDownloader::CreateConnecter() // OK
{
    if (m_pServerInfo->GetDownloaderType() == HTTP)
    {
        return new HTTPConnecter(m_pServerInfo, m_pFileInfo);
    }
    else
    {
        return new FTPConnecter(m_pServerInfo, m_pFileInfo);
    }
}

WZResult FileDownloader::CreateConnection()
{
    DWORD dwMilliseconds = this->m_pServerInfo->GetConnectTimeout();

    if (dwMilliseconds > 0)
    {
        unsigned int ThreadID = 0;

        auto hHandle =
            (HANDLE)_beginthreadex(0, 0, FileDownloader::RunConnectThread, this, 0, &ThreadID);

        if (hHandle == INVALID_HANDLE_VALUE)
        {
            this->m_Result.SetResult(
                DL_BEGIN_THREAD_CONNECTION, GetLastError(),
                L"[FileDownloader::CreateConnection] Fail : _beginthreadex, FileName = %ls",
                this->m_pFileInfo->GetRemoteFilePath());
        }
        else
        {
            if (WaitForSingleObject(hHandle, dwMilliseconds) == WAIT_TIMEOUT)
            {
                InternetCloseHandle(this->m_hSession);
                this->m_hSession = 0;

                WaitForSingleObject(hHandle, INFINITE);

                CloseHandle(hHandle);

                this->m_Result.SetResult(
                    DL_CONNECTION_TIMEOUT, 0,
                    L"[FileDownloader::CreateConnection] Fail : WAIT_TIMEOUT, FileName = %ls",
                    this->m_pFileInfo->GetRemoteFilePath());
            }
            else
            {
                CloseHandle(hHandle);
            }
        }
    }
    else
    {
        this->m_Result = this->Connection();
    }

    return this->m_Result;
}

unsigned int WINAPI FileDownloader::RunConnectThread(LPVOID pParam)
{
    auto *p = reinterpret_cast<FileDownloader *>(pParam);

    if (p)
    {
        p->m_Result = p->Connection();
    }

    return 0;
}

WZResult FileDownloader::Connection()
{
    this->m_Result = this->m_pConnecter->CreateConnection(this->m_hSession, this->m_hConnection);

    return this->m_Result;
}

WZResult FileDownloader::TransferRemoteFile()
{
    DWORD CbSize = this->m_pServerInfo->GetReadBufferSize();

    BYTE *buffer = new BYTE[CbSize];

    DWORD TotalSize = 0;
    DWORD ReadSize = 0;

    this->SendStartedDownloadFileEvent(this->m_nFileLength);

    ReadSize = 0;
    this->m_Result = this->m_pConnecter->ReadRemoteFile(this->m_hRemoteFile, buffer, &ReadSize);

    if (this->CanBeContinue())
    {
        while (true)
        {
            if (ReadSize > 0)
            {
                this->m_Result = this->WriteLocalFile(buffer, ReadSize);

                if (!this->CanBeContinue())
                    break;

                TotalSize += ReadSize;
                this->SendProgressDownloadFileEvent(TotalSize);
            }

            if (ReadSize == 0 || this->m_bBreak)
            {
                if (this->CanBeContinue())
                {
                    if (TotalSize >= this->m_nFileLength)
                    {
                        this->m_Result.SetSuccessResult();
                    }
                    else
                    {
                        this->m_Result.SetResult(
                            DL_DIFFERENT_FILE_LENGTH, 0,
                            L"[FileDownloader::TransferRemoteFile] Fail : Different Down File Size, FileName = %ls",
                            this->m_pFileInfo->GetRemoteFilePath());
                    }
                }

                break;
            }
        }
    }

    this->SendCompletedDownloadFileEvent(this->m_Result);

    delete[] buffer;

    return this->m_Result;
}

WZResult FileDownloader::CreateLocalFile()
{
    TCHAR *path = this->m_pFileInfo->GetLocalFilePath();

    if (GetFileAttributes(path) == INVALID_FILE_ATTRIBUTES || this->m_pServerInfo->IsOverWrite())
    {
        Path::CreateDirectorys(path, 1);

        path = this->m_pFileInfo->GetLocalFilePath();

        DWORD attr = GetFileAttributes(path);
        if ((attr & 1) != 0)
        {
            SetFileAttributes(path, attr & 0xFFFFFFFE);
        }
        this->m_hLocalFile = CreateFile(path, 0x40000000, 0, 0, CREATE_ALWAYS, 0x80, 0);

        if (this->m_hLocalFile == INVALID_HANDLE_VALUE)
        {
            this->m_Result.SetResult(
                DL_CREATE_LOCALFILE, GetLastError(),
                L"[FileDownloader::CreateLocalFile] Fail : CreateFile, FileName = %ls",
                this->m_pFileInfo->GetRemoteFilePath());
        }
        else
        {
            this->m_Result.SetSuccessResult();
        }
    }
    else
    {
        this->m_Result.SetResult(
            DL_LOCALFILE_EXISTS, 0,
            L"[FileDownloader::CreateLocalFile] Fail : Local File Exists, FileName = %ls",
            this->m_pFileInfo->GetRemoteFilePath());
    }

    return this->m_Result;
}

WZResult FileDownloader::ReadRemoteFile(BYTE *byReadBuffer, DWORD *dwBytesRead)
{
    if (this->m_hRemoteFile)
    {
        this->m_Result =
            this->m_pConnecter->ReadRemoteFile(this->m_hRemoteFile, byReadBuffer, dwBytesRead);
    }
    else
    {
        this->m_Result.SetResult(
            DL_READ_REMOTEFILE, 0,
            L"[FileDownloader::ReadRemoteFile] Fail : ReadRemoteFile, FileName = %ls",
            this->m_pFileInfo->GetRemoteFilePath());
    }

    return this->m_Result;
}

WZResult FileDownloader::WriteLocalFile(BYTE *byReadBuffer, DWORD dwBytesRead)
{
    DWORD NumberOfBytesWritten = 0;

    if (WriteFile(this->m_hLocalFile, byReadBuffer, dwBytesRead, &NumberOfBytesWritten, 0) &&
        dwBytesRead == NumberOfBytesWritten)
    {
        this->m_Result.SetSuccessResult();
    }
    else
    {
        this->m_Result.SetResult(
            DL_WRITE_LOCALFILE, GetLastError(),
            L"[FileDownloader::WriteLocalFile] Fail : WriteFile, FileName = %ls",
            this->m_pFileInfo->GetRemoteFilePath());
    }

    return this->m_Result;
}

void FileDownloader::SendStartedDownloadFileEvent(ULONGLONG nFileLength)
{
    if (this->m_pStateEvent != NULL)
    {
        this->m_pStateEvent->OnStartedDownloadFile(this->m_pFileInfo->GetFileName(), nFileLength);
    }
}

void FileDownloader::SendCompletedDownloadFileEvent(WZResult wzResult)
{
    if (this->m_pStateEvent != NULL)
    {
        this->m_pStateEvent->OnCompletedDownloadFile(this->m_pFileInfo->GetFileName(), wzResult);
    }
}

void FileDownloader::SendProgressDownloadFileEvent(ULONGLONG nTotalBytesRead)
{
    if (this->m_pStateEvent != NULL)
    {
        this->m_pStateEvent->OnProgressDownloadFile(this->m_pFileInfo->GetFileName(),
                                                    nTotalBytesRead);
    }
}

#endif

#endif // _WIN32

#ifdef _WIN32
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

HTTPConnecter::HTTPConnecter(DownloadServerInfo *pServerInfo, DownloadFileInfo *pFileInfo)
    : IConnecter(pServerInfo, pFileInfo)
{
}

HTTPConnecter::~HTTPConnecter()
{
}

WZResult HTTPConnecter::CreateSession(HINTERNET &hSession)
{
    wchar_t path[MAX_PATH] = {0};

    Path::GetCurrentFileName(path);

    if ((hSession = InternetOpen(path, 0, 0, 0, 0)))
    {
        this->m_Result.SetSuccessResult();
    }
    else
    {
        this->m_Result.SetResult(
            DL_CREATE_SESSION, GetLastError(),
            L"[HTTPConnecter::CreateSession] Fail : InternetOpen, FileName = %ls",
            this->m_pFileInfo->GetRemoteFilePath());
    }

    return this->m_Result;
}

WZResult HTTPConnecter::CreateConnection(HINTERNET &hSession, HINTERNET &hConnection)
{
    this->m_pServerInfo->GetPassword();

    if ((hConnection =
             InternetConnect(hSession, this->m_pServerInfo->GetServerURL(),
                             this->m_pServerInfo->GetPort(), this->m_pServerInfo->GetUserID(),
                             this->m_pServerInfo->GetPassword(), 3, 0, (DWORD_PTR)this)))
    {
        this->m_Result.SetSuccessResult();
    }
    else
    {
        this->m_Result.SetResult(
            DL_CREATE_CONNECTION, GetLastError(),
            L"[HTTPConnecter::CreateConnection] Fail : InternetConnect, FileName = %ls",
            this->m_pFileInfo->GetRemoteFilePath());
    }

    return this->m_Result;
}

WZResult HTTPConnecter::OpenRemoteFile(HINTERNET &hConnection, HINTERNET &hRemoteFile,
                                       ULONGLONG &nFileLength)
{
    hRemoteFile = HttpOpenRequest(hConnection, L"GET", this->m_pFileInfo->GetRemoteFilePath(),
                                  L"HTTP/1.0", 0, 0, 0x80000000, 0);

    if (hRemoteFile)
    {
        HttpSendRequest(hRemoteFile, 0, 0, 0, 0);

        wchar_t buffer[32];

        memset(buffer, 0, sizeof(buffer));

        DWORD dwBufferLength = 32;

        if (HttpQueryInfo(hRemoteFile, HTTP_QUERY_STATUS_CODE, buffer, &dwBufferLength, 0))
        {
            if (_wtoi64(buffer) == HTTP_STATUS_OK)
            {
                memset(buffer, 0, sizeof(buffer));
                dwBufferLength = 32;

                if (HttpQueryInfo(hRemoteFile, HTTP_QUERY_CONTENT_LENGTH, buffer, &dwBufferLength,
                                  0))
                {
                    nFileLength = _wtoi64(buffer);
                    this->m_Result.SetSuccessResult();
                }
                else
                {
                    this->m_Result.SetResult(
                        DL_GET_FILE_LENGTH, GetLastError(),
                        L"[HTTPConnecter::OpenRemoteFile] Fail : HttpQueryInfo - HTTP_QUERY_CONTENT_LENGTH, FileName = %ls",
                        this->m_pFileInfo->GetRemoteFilePath());
                }
            }
            else
            {
                this->m_Result.SetResult(
                    DL_HTTP_STATUS_NOT_OK, 0,
                    L"[HTTPConnecter::OpenRemoteFile] Fail : Not HTTP_STATUS_OK, FileName = %ls",
                    this->m_pFileInfo->GetRemoteFilePath());
            }
        }
        else
        {
            this->m_Result.SetResult(
                DL_HTTP_QUERY_INFO, GetLastError(),
                L"[HTTPConnecter::OpenRemoteFile] Fail : HttpQueryInfo - HTTP_QUERY_STATUS_CODE, FileName = %ls",
                this->m_pFileInfo->GetRemoteFilePath());
        }
    }
    else
    {
        this->m_Result.SetResult(
            DL_OPEN_REMOTEFILE, GetLastError(),
            L"[HTTPConnecter::OpenRemoteFile] Fail : HttpOpenRequest, FileName = %ls",
            this->m_pFileInfo->GetRemoteFilePath());
    }

    return this->m_Result;
}

WZResult HTTPConnecter::ReadRemoteFile(HINTERNET &hRemoteFile, BYTE *byReadBuffer,
                                       DWORD *dwBytesRead)
{
    DWORD dwNumberOfBytesAvailable = 0;

    InternetQueryDataAvailable(hRemoteFile, &dwNumberOfBytesAvailable, 0, 0);

    DWORD Size = this->m_pServerInfo->GetReadBufferSize();

    if (InternetReadFile(hRemoteFile, byReadBuffer, Size, dwBytesRead))
    {
        this->m_Result.SetSuccessResult();
    }
    else
    {
        this->m_Result.SetResult(
            DL_READ_REMOTEFILE, GetLastError(),
            L"[HTTPConnecter::ReadRemoteFile] Fail : InternetReadFile, FileName = %ls",
            this->m_pFileInfo->GetRemoteFilePath());
    }

    return this->m_Result;
}
#endif

#endif // _WIN32

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CInGameShopSystem::CInGameShopSystem(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), g_ConsoleDebug(keeper.ConsoleDebug()), m_ShopManager(keeper)
{
    m_pCategoryList = NULL;
    m_pPackageList = NULL;
    m_pProductList = NULL;
    m_pBannerList = NULL;

    memset(&m_ScriptVerInfo, -1, sizeof(CListVersionInfo));
    memset(&m_BannerVerInfo, -1, sizeof(CListVersionInfo));
    memset(&m_CurrentScriptVerInfo, -1, sizeof(CListVersionInfo));
    memset(&m_CurrentBannerVerInfo, -1, sizeof(CListVersionInfo));

    m_bIsShopOpenLock = true; //louis
    m_bIsBanner = false;
    m_bIsRequestEventPackage = false;
    m_plistSelectPackage = NULL;
    m_bFirstScriptDownloaded = false;
    m_bFirstBannerDownloaded = false;
}

CInGameShopSystem::~CInGameShopSystem()
{
    Release();
}

CInGameShopSystem *CreateSessionInGameShopSystem(SessionKeeper &keeper)
{
    return new CInGameShopSystem(keeper);
}

void DestroySessionInGameShopSystem(CInGameShopSystem *system) noexcept
{
    delete system;
}

void CInGameShopSystem::Initalize()
{
    m_mapZoneSeqIndex.clear();
    m_listDisplayPackage.clear();
    m_listNormalPackage.clear();
    m_listEventPackage.clear();
    m_listZoneName.clear();
    m_listCategoryName.clear();
    m_plistSelectPackage = &m_listNormalPackage;
    m_dTotalCash = 0;
    m_dTotalPoint = 0;
    m_dTotalMileage = 0;
    m_dCashCreditCard = 0;
    m_dCashPrepaid = 0;
    m_iEventPackageCnt = 0;
    m_iSelectedPage = 1;
    m_iTotalEventPackage = 0;
    m_iCntSelectEventZone = 0;
    m_bSelectEventCategory = false;
    m_bIsRequestShopOpenning = false;
    m_bAbleRequestEventPackage = true;
    InitZoneInfo();
}

void CInGameShopSystem::Release()
{
    m_mapZoneSeqIndex.clear();
    m_listDisplayPackage.clear();
    m_listNormalPackage.clear();
    m_listEventPackage.clear();
    m_listZoneName.clear();
    m_listCategoryName.clear();
    m_pCategoryList = NULL;
    m_pPackageList = NULL;
    m_pProductList = NULL;
}

void CInGameShopSystem::SetScriptVersion(int iSalesZone, int iYear, int iYearId)
{
    m_ScriptVerInfo.Zone = iSalesZone;
    m_ScriptVerInfo.year = iYear;
    m_ScriptVerInfo.yearId = iYearId;
}

void CInGameShopSystem::SetBannerVersion(int iSalesZone, int iYear, int iYearId)
{
    m_BannerVerInfo.Zone = iSalesZone;
    m_BannerVerInfo.year = iYear;
    m_BannerVerInfo.yearId = iYearId;
}

bool CInGameShopSystem::ScriptDownload()
{
    m_bFirstScriptDownloaded = true;

    ::GetCurrentDirectory(255, m_szScriptLocalPath);

    wchar_t szScriptRemotePathforDMZ[MAX_TEXT_LENGTH];
    mu_swprintf(m_szScriptLocalPath, L"%ls%ls", m_szScriptLocalPath, L"\\data\\InGameShopScript");
    wcscpy(m_szScriptIPAddress, L"image.webzen.com");
    wcscpy(m_szScriptRemotePath, L"/Global/Payment/ProductTransfer");
    wcscpy(szScriptRemotePathforDMZ, L"/Global/Payment/DevScriptGB/ProductTransfer");

#ifdef FOR_WORK
    HANDLE hFile;
    hFile = CreateFile(L"dmz.ini",            // file to create
                       GENERIC_READ,          // open for reading
                       0,                     // do not share
                       NULL,                  // default security
                       OPEN_EXISTING,         // existing file only
                       FILE_ATTRIBUTE_NORMAL, // normal file
                       NULL);                 // no template

    if (hFile != INVALID_HANDLE_VALUE)
    {
        wcscpy(m_szScriptRemotePath, szScriptRemotePathforDMZ);
    }
    CloseHandle(hFile);
#endif // FOR_WORK
    m_ShopManager.SetListManagerInfo(HTTP, m_szScriptIPAddress, L"", L"", m_szScriptRemotePath,
                                     m_szScriptLocalPath, m_ScriptVerInfo, 10000);

    WZResult res = m_ShopManager.LoadScriptList(false);

    if (!res.IsSuccess())
    {
        m_pCategoryList = NULL;
        m_pPackageList = NULL;
        m_pProductList = NULL;

        ShopOpenLock();

        wchar_t szText[MAX_TEXT_LENGTH] = {
            '\0',
        };
        mu_swprintf(szText, I18N::Game::MUItemShopInformationDownloadFailed, m_ScriptVerInfo.Zone,
                    m_ScriptVerInfo.year, m_ScriptVerInfo.yearId, res.GetErrorMessage());
        CMsgBoxIGSCommon *pMsgBox = NULL;
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(CMsgBoxIGSCommonLayout, sessionKeeper_), &pMsgBox);
        pMsgBox->Initialize(I18N::Game::Error, szText);
        return false;
    }

    m_CurrentScriptVerInfo = m_ScriptVerInfo;

#ifdef CONSOLE_DEBUG
    g_ConsoleDebug.Write(MCD_NORMAL,
                         L"InGameShopStatue.Txt <IngameShop Script Download Success!!!>");
    g_ConsoleDebug.Write(MCD_NORMAL, L"InGameShopStatue.Txt - Ver %d.%d.%d", m_ScriptVerInfo.Zone,
                         m_ScriptVerInfo.year, m_ScriptVerInfo.yearId);
#endif
    ShopOpenUnLock();

    CShopList *pShopList = m_ShopManager.GetListPtr();

    m_pCategoryList = pShopList->GetCategoryListPtr();
    m_pPackageList = pShopList->GetPackageListPtr();
    m_pProductList = pShopList->GetProductListPtr();

    return true;
}

bool CInGameShopSystem::BannerDownload()
{
    m_bFirstBannerDownloaded = true;

    ::GetCurrentDirectory(255, m_szBannerLocalPath);

    wchar_t szBannerRemotePathforDMZ[MAX_TEXT_LENGTH];
    mu_swprintf(m_szBannerLocalPath, L"%ls%ls", m_szBannerLocalPath, L"\\data\\InGameShopBanner");

    wcscpy(m_szBannerIPAddress, L"image.webzen.com");
    wcscpy(m_szBannerRemotePath, L"/Global/Payment/BannerTransfer");
    wcscpy(szBannerRemotePathforDMZ, L"/Global/Payment/DevScriptGB/BannerTransfer");

#ifdef FOR_WORK
    HANDLE hFile;
    hFile = CreateFile(L"dmz.ini",            // file to create
                       GENERIC_READ,          // open for reading
                       0,                     // do not share
                       NULL,                  // default security
                       OPEN_EXISTING,         // existing file only
                       FILE_ATTRIBUTE_NORMAL, // normal file
                       NULL);                 // no template

    if (hFile != INVALID_HANDLE_VALUE)
    {
        wcscpy(m_szBannerRemotePath, szBannerRemotePathforDMZ);
    }
    CloseHandle(hFile);
#endif // FOR_WORK

#ifdef KJH_MOD_SHOP_SCRIPT_DOWNLOAD
    m_BannerManager.SetListManagerInfo(HTTP, m_szBannerIPAddress, L"", L"", m_szBannerRemotePath,
                                       m_szBannerLocalPath, m_BannerVerInfo, 4000);
#else  // KJH_MOD_SHOP_SCRIPT_DOWNLOAD
    m_BannerManager.SetListManagerInfo(HTTP, m_szIPAddress, "", "", m_szBannerRemotePath,
                                       m_szBannerLocalPath, m_BannerVerInfo);
#endif // KJH_MOD_SHOP_SCRIPT_DOWNLOAD

    // DownLoad & Load
    WZResult res = m_BannerManager.LoadScriptList(false);

    // DownLoad & Load
    if (!res.IsSuccess())
    {
        m_pBannerList = NULL;
        m_bIsBanner = false;

        // MessageBox
        wchar_t szText[MAX_TEXT_LENGTH] = {
            '\0',
        };
        mu_swprintf(szText, I18N::Game::BannerDownloadFailedVersionDDDS, m_BannerVerInfo.Zone,
                    m_BannerVerInfo.year, m_BannerVerInfo.yearId, res.GetErrorMessage());
        CMsgBoxIGSCommon *pMsgBox = NULL;
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(CMsgBoxIGSCommonLayout, sessionKeeper_), &pMsgBox);
        pMsgBox->Initialize(I18N::Game::Error, szText);

        return false;
    }

    m_CurrentBannerVerInfo = m_BannerVerInfo;
    m_pBannerList = m_BannerManager.GetListPtr();

    m_pBannerList->SetFirst();
    if (m_pBannerList->GetNext(m_BannerInfo) == false)
        return false;

    m_bIsBanner = true;
    return true;
}

#ifdef KJH_MOD_SHOP_SCRIPT_DOWNLOAD
bool CInGameShopSystem::IsScriptDownload()
{
#ifdef CONSOLE_DEBUG
    g_ConsoleDebug.Write(MCD_NORMAL,
                         L"InGameShopStatue.Txt CallStack - CInGameShopSystem::IsScriptDownload()");
    g_ConsoleDebug.Write(MCD_NORMAL, L"InGameShopStatue.Txt - Script Ver %d.%d.%d",
                         m_ScriptVerInfo.Zone, m_ScriptVerInfo.year, m_ScriptVerInfo.yearId);
    g_ConsoleDebug.Write(MCD_NORMAL, L"InGameShopStatue.Txt - Current Ver %d.%d.%d",
                         m_CurrentScriptVerInfo.Zone, m_CurrentScriptVerInfo.year,
                         m_CurrentScriptVerInfo.yearId);
#endif
    if (((m_ScriptVerInfo.year == m_CurrentScriptVerInfo.year) &&
         (m_ScriptVerInfo.yearId == m_CurrentScriptVerInfo.yearId) &&
         (m_ScriptVerInfo.Zone == m_CurrentScriptVerInfo.Zone)) &&
        (m_bFirstScriptDownloaded == true))
    {
#ifdef CONSOLE_DEBUG
        g_ConsoleDebug.Write(MCD_NORMAL, L"InGameShopStatue.Txt Return - false");
#endif
        return false;
    }
#ifdef CONSOLE_DEBUG
    g_ConsoleDebug.Write(MCD_NORMAL, L"InGameShopStatue.Txt Return - true");
#endif
    return true;
}

bool CInGameShopSystem::IsBannerDownload()
{
    if (((m_BannerVerInfo.year == m_CurrentBannerVerInfo.year) &&
         (m_BannerVerInfo.yearId == m_CurrentBannerVerInfo.yearId) &&
         (m_BannerVerInfo.Zone == m_CurrentBannerVerInfo.Zone)) &&
        (m_bFirstBannerDownloaded == true))
    {
        return false;
    }

    return true;
}
#endif // KJH_MOD_SHOP_SCRIPT_DOWNLOAD

bool CInGameShopSystem::SelectZone(int iIndex)
{
    int iZoneSeqIndex = GetZoneSeqIndexByIndex(iIndex);
    if ((INGAMESHOP_ERROR_ZERO_SIZE == iZoneSeqIndex) ||
        (INGAMESHOP_ERROR_INVALID_INDEX == iZoneSeqIndex))
    {
        return false;
    }

    if (m_pCategoryList->GetValueByKey(iZoneSeqIndex, m_SelectedZone))
    {
        SetCategoryName();

        return true;
    }
    return false;
}

bool CInGameShopSystem::SelectCategory(int iIndex)
{
    m_listDisplayPackage.clear();

    int iCategorySeqIndex = GetCategorySeqIndexByIndex(iIndex);
    if ((INGAMESHOP_ERROR_ZERO_SIZE == iCategorySeqIndex) ||
        (INGAMESHOP_ERROR_INVALID_INDEX == iCategorySeqIndex))
    {
        return false;
    }

    if (m_pCategoryList->GetValueByKey(iCategorySeqIndex, m_SelectedCategory))
    {
        if (m_SelectedZone.EventFlag == 199 && m_SelectedCategory.EventFlag == 199)
        {
            m_bSelectEventCategory = true;
            m_plistSelectPackage = &m_listEventPackage;
            SocketClient->ToGameServer()->SendCashShopEventItemListRequest(
                m_SelectedCategory.ProductDisplaySeq);
            m_bIsRequestEventPackage = true;
        }
        else
        {
            m_bSelectEventCategory = false;
            m_plistSelectPackage = &m_listNormalPackage;
            SetNormalPackage();
        }

        return true;
    }

    return false;
}

void CInGameShopSystem::BeginPage()
{
    m_iSelectedPage = 1;
    InitPackagePerPage(m_iSelectedPage);
}

void CInGameShopSystem::NextPage()
{
    if (GetTotalPages() > m_iSelectedPage)
    {
        m_iSelectedPage++;
        InitPackagePerPage(m_iSelectedPage);
    }
}

void CInGameShopSystem::PrePage()
{
    if (m_iSelectedPage > 1)
    {
        m_iSelectedPage--;
        InitPackagePerPage(m_iSelectedPage);
    }
}

int CInGameShopSystem::GetTotalPages()
{
    return (m_plistSelectPackage->size() / INGAMESHOP_DISPLAY_ITEMLIST_SIZE) + 1;
}

int CInGameShopSystem::GetSelectPage()
{
    return m_iSelectedPage;
}

void CInGameShopSystem::SetNormalPackage()
{
    CShopPackage Package;
    int iPackageSeqIndex;

    m_listNormalPackage.clear();

    m_SelectedCategory.SetPackagSeqFirst();

    while (m_SelectedCategory.GetPackagSeqNext(iPackageSeqIndex))
    {
        if (!m_pPackageList->GetValueByKey(iPackageSeqIndex, Package))
            break;

        m_listNormalPackage.push_back(Package);
    }
    BeginPage();
}

void CInGameShopSystem::InitEventPackage(int iTotalEventPackage)
{
    m_listEventPackage.clear();
    m_listDisplayPackage.clear();
    m_iTotalEventPackage = iTotalEventPackage;
    m_iEventPackageCnt = 0;
    m_iCurrentEventPackage = 0;

    if (m_iTotalEventPackage < 1)
    {
        m_bIsRequestEventPackage = false;
    }
}

void CInGameShopSystem::InsertEventPackage(int *pPackageSeq)
{
    m_SelectedCategory.SetPackagSeqFirst();

    CShopPackage Package;

    for (int i = 0; i < INGAMESHOP_DISPLAY_ITEMLIST_SIZE; i++)
    {
        if (m_pPackageList->GetValueByKey(pPackageSeq[i], Package))
        {
            m_listEventPackage.push_back(Package);
        }

        m_iCurrentEventPackage++;

        if (m_iTotalEventPackage == m_iCurrentEventPackage)
        {
            BeginPage();
            m_bIsRequestEventPackage = false;
            return;
        }
    }
}

int CInGameShopSystem::GetSizeZones()
{
    return m_mapZoneSeqIndex.size();
}

int CInGameShopSystem::GetSizeCategoriesAsSelectedZone()
{
    return m_SelectedZone.CategoryList.size();
}

int CInGameShopSystem::GetSizePackageAsSelectedCategory()
{
    return m_plistSelectPackage->size();
}

int CInGameShopSystem::GetSizePackageAsDisplayPackage()
{
    return m_listDisplayPackage.size();
}

type_listName &CInGameShopSystem::GetZoneName()
{
    return m_listZoneName;
}

type_listName &CInGameShopSystem::GetCategoryName()
{
    return m_listCategoryName;
}

WORD CInGameShopSystem::GetPackageItemCode(int iIndex)
{
    auto iterPackage = m_listDisplayPackage.begin();

    for (int i = 0; i < (int)m_listDisplayPackage.size(); i++)
    {
        if (iterPackage == m_listDisplayPackage.end())
            return -1;

        if (i == iIndex)
            break;

        iterPackage++;
    }

    return _wtoi((*iterPackage).InGamePackageID);
}

void CInGameShopSystem::SetTotalCash(double dTotalCash)
{
    m_dTotalCash = dTotalCash;
}

void CInGameShopSystem::SetTotalPoint(double dTotalPoint)
{
    m_dTotalPoint = dTotalPoint;
}

void CInGameShopSystem::SetTotalMileage(double dTotalMileage)
{
    m_dTotalMileage = dTotalMileage;
}

void CInGameShopSystem::SetCashCreditCard(double dCashCreditCard)
{
    m_dCashCreditCard = dCashCreditCard;
}

void CInGameShopSystem::SetCashPrepaid(double dCashPrepaid)
{
    m_dCashPrepaid = dCashPrepaid;
}

double CInGameShopSystem::GetTotalCash()
{
    return m_dTotalCash;
}

double CInGameShopSystem::GetTotalPoint()
{
    return m_dTotalPoint;
}

double CInGameShopSystem::GetTotalMileage()
{
    return m_dTotalMileage;
}

double CInGameShopSystem::GetCashCreditCard()
{
    return m_dCashCreditCard;
}

double CInGameShopSystem::GetCashPrepaid()
{
    return m_dCashPrepaid;
}

CShopPackage *CInGameShopSystem::GetDisplayPackage(int iIndex)
{
    auto iterPackage = m_listDisplayPackage.begin();

    for (int i = 0; i < (int)m_listDisplayPackage.size(); i++)
    {
        if (iterPackage == m_listDisplayPackage.end())
            return NULL;

        if (i == iIndex)
            break;

        iterPackage++;
    }

    return &(*iterPackage);
}

void CInGameShopSystem::SetIsRequestShopOpenning(bool IsRequestShopOpenning)
{
    m_bIsRequestShopOpenning = IsRequestShopOpenning;
}

bool CInGameShopSystem::GetIsRequestShopOpenning()
{
    return m_bIsRequestShopOpenning;
}

bool CInGameShopSystem::GetPackageInfo(int iPackageSeq, int iPackageAttrType, OUT int &iValue,
                                       OUT wchar_t *pszText)
{
    CShopPackage Package;

    if (m_pPackageList->GetValueByKey(iPackageSeq, Package) == true)
    {
        switch (iPackageAttrType)
        {
        case IGS_PACKAGE_ATT_TYPE_NAME: {
            iValue = 0;
            wcscpy(pszText, Package.PackageProductName);
            return true;
        }
        break;
        case IGS_PACKAGE_ATT_TYPE_DESCRIPTION: {
            iValue = 0;
            wcscpy(pszText, Package.Description);
            return true;
        }
        break;
        case IGS_PACKAGE_ATT_TYPE_PRICE: {
            wchar_t szText[MAX_TEXT_LENGTH] = {
                '\0',
            };
            iValue = Package.Price;
            ConvertGold(iValue, szText);
            mu_swprintf(pszText, L"%ls %ls", szText, Package.PricUnitName);
            return true;
        }
        break;
        case IGS_PACKAGE_ATT_TYPE_ITEMCODE: {
            iValue = _wtoi(Package.InGamePackageID);
            pszText[0] = '\0';
            return true;
        }
        break;
        default: {
            iValue = 0;
            pszText[0] = '\0';
        }
        break;
        }
    }

    return false;
}

bool CInGameShopSystem::GetProductInfoFromPriceSeq(int iProductSeq, int iPriceSeq, int iAttrType,
                                                   OUT int &iValue, OUT wchar_t *pszUnitName)
{
    CShopProduct Product;

    m_pProductList->SetPriceSeqFirst(iProductSeq, iPriceSeq);

    while (m_pProductList->GetPriceSeqNext(Product))
    {
        if (GetProductInfo(&Product, iAttrType, iValue, pszUnitName) == true)
        {
            return true;
        }
    }

    iValue = -1;
    pszUnitName[0] = '\0';

    return false;
}

bool CInGameShopSystem::GetProductInfoFromProductSeq(int iProductSeq, int iAttrType,
                                                     OUT int &iValue, OUT wchar_t *pszUnitName)
{
    CShopProduct Product;

    m_pProductList->SetProductSeqFirst(iProductSeq);

    while (m_pProductList->GetProductSeqNext(Product))
    {
        if (GetProductInfo(&Product, iAttrType, iValue, pszUnitName) == true)
        {
            return true;
        }
    }

    iValue = -1;
    pszUnitName[0] = '\0';

    return false;
}

bool CInGameShopSystem::GetProductInfo(CShopProduct *pProduct, int iAttrType, OUT int &iValue,
                                       OUT wchar_t *pszUnitName)
{
    switch (iAttrType)
    {
    case IGS_PRODUCT_ATT_TYPE_USE_LIMIT_PERIOD: {
        if ((pProduct->PropertySeq == 2) || (pProduct->PropertySeq == 28) ||
            (pProduct->PropertySeq == 12) || (pProduct->PropertySeq == 58) ||
            (pProduct->PropertySeq == 10))
        {
            iValue = _wtoi(pProduct->Value);
            switch (pProduct->UnitType)
            {
            case 386: {
                if (iValue >= 86400)
                {
                    iValue /= 86400;
                    wcscpy(pszUnitName, I18N::Game::Day);
                }
                else if (iValue >= 3600)
                {
                    iValue /= 3600;
                    wcscpy(pszUnitName, I18N::Game::Hour);
                }
                else if (iValue >= 60)
                {
                    iValue /= 60;
                    wcscpy(pszUnitName, I18N::Game::Minute);
                }
                else
                {
                    wcscpy(pszUnitName, I18N::Game::Second);
                }
            }
            break;
            case 174: {
                if (iValue >= 1440)
                {
                    iValue /= 1440;
                    wcscpy(pszUnitName, I18N::Game::Day);
                }
                else if (iValue >= 60)
                {
                    iValue /= 60;
                    wcscpy(pszUnitName, I18N::Game::Hour);
                }
                else
                {
                    wcscpy(pszUnitName, I18N::Game::Minute);
                }
            }
            break;
            case 172: {
                if (iValue >= 24)
                {
                    iValue /= 24;
                    wcscpy(pszUnitName, I18N::Game::Day);
                }
                else
                {
                    wcscpy(pszUnitName, I18N::Game::Hour);
                }
            }
            break;
            default: {
                wcscpy(pszUnitName, pProduct->UnitName);
            }
            break;
            }
            return true;
        }
    }
    break;
    case IGS_PRODUCT_ATT_TYPE_AVALIABLE_PERIOD: {
        if ((pProduct->PropertySeq == 46) || (pProduct->PropertySeq == 49) ||
            (pProduct->PropertySeq == 48) || (pProduct->PropertySeq == 51) ||
            (pProduct->PropertySeq == 52) || (pProduct->PropertySeq == 53) ||
            (pProduct->PropertySeq == 50) || (pProduct->PropertySeq == 60))
        {
            iValue = _wtoi(pProduct->Value);
            wcscpy(pszUnitName, pProduct->UnitName);
            return true;
        }
    }
    break;
    case IGS_PRODUCT_ATT_TYPE_NUM: {
        if ((pProduct->PropertySeq == 30) || (pProduct->PropertySeq == 11) ||
            (pProduct->PropertySeq == 7) || (pProduct->PropertySeq == 8) ||
            (pProduct->PropertySeq == 9) || (pProduct->PropertySeq == 31))
        {
            iValue = _wtoi(pProduct->Value);
            wcscpy(pszUnitName, pProduct->UnitName);
            return true;
        }
    }
    break;
    case IGS_PRODUCT_ATT_TYPE_PRICE: {
        iValue = pProduct->Price;
        ConvertGold(pProduct->Price, pszUnitName);
        return true;
    }
    break;
    case IGS_PRODUCT_ATT_TYPE_ITEMCODE: {
        iValue = _wtoi(pProduct->InGamePackageID);
        pszUnitName[0] = '\0';
        return true;
    }
    break;
    case IGS_PRODUCT_ATT_TYPE_ITEMNAME: {
        iValue = -1;
        wcscpy(pszUnitName, pProduct->ProductName);
        return true;
    }
    break;
    case IGS_PRODUCT_ATT_TYPE_PRICE_SEQUENCE: {
        iValue = pProduct->PriceSeq;
        pszUnitName[0] = '\0';
        return true;
    }
    break;
    default: {
        iValue = -1;
        pszUnitName[0] = '\0';
    }
    break;
    }

    return false;
}

bool CInGameShopSystem::IsRequestEventPackge()
{
    if (m_bIsRequestEventPackage == true)
        return false;

    return true;
}

void CInGameShopSystem::SetRequestEventPackge()
{
    m_bIsRequestEventPackage = false;
}

bool CInGameShopSystem::IsShopOpen()
{
    return m_bIsShopOpenLock ? false : true;
}

bool CInGameShopSystem::IsBanner()
{
    return m_bIsBanner;
}

wchar_t *CInGameShopSystem::GetBannerFileName()
{
    if (m_bIsBanner == false)
        return NULL;

    return m_BannerInfo.BannerImagePath;
}

wchar_t *CInGameShopSystem::GetBannerURL()
{
    if (m_bIsBanner == false)
        return NULL;

    return m_BannerInfo.BannerLinkURL;
}

void CInGameShopSystem::InitZoneInfo()
{
    m_mapZoneSeqIndex.clear();
    m_listZoneName.clear();

    m_pCategoryList->SetFirst();
    CShopCategory Zone;

    int i = 0;
    while (m_pCategoryList->GetNext(Zone))
    {
        if (1 == Zone.Root)
        {
            m_mapZoneSeqIndex.insert(type_mapZoneSeq::value_type(i++, Zone.ProductDisplaySeq));
            m_listZoneName.push_back(Zone.CategroyName);
        }
    }
}

void CInGameShopSystem::InitPackagePerPage(int iPageIndex)
{
    m_listDisplayPackage.clear();

    type_listPackage::iterator iterlistPackage;

    iterlistPackage = m_plistSelectPackage->begin();

    int iBeginDisplayItemIndex = INGAMESHOP_DISPLAY_ITEMLIST_SIZE * (iPageIndex - 1);
    for (int i = 0; i < iBeginDisplayItemIndex; i++)
    {
        iterlistPackage++;
    }

    for (int j = 0; j < INGAMESHOP_DISPLAY_ITEMLIST_SIZE; j++)
    {
        if (iterlistPackage == m_plistSelectPackage->end())
            break;

        m_listDisplayPackage.push_back(*iterlistPackage);
        iterlistPackage++;
    }
}

int CInGameShopSystem::GetZoneSeqIndexByIndex(int iIndex)
{
    if (GetSizeZones() <= 0)
        return INGAMESHOP_ERROR_ZERO_SIZE;

    auto iterZoneSeqIndex = m_mapZoneSeqIndex.find(iIndex);

    if (iterZoneSeqIndex == m_mapZoneSeqIndex.end())
        return INGAMESHOP_ERROR_INVALID_INDEX;

    return (int)iterZoneSeqIndex->second;
}

int CInGameShopSystem::GetCategorySeqIndexByIndex(int iIndex)
{
    int iCategorySeqIndex = 0;
    bool bRes = false;

    if (GetSizeCategoriesAsSelectedZone() <= 0)
        return INGAMESHOP_ERROR_ZERO_SIZE;

    m_SelectedZone.SetCategoryFirst();
    for (int i = 0; i <= iIndex; i++)
    {
        bRes = m_SelectedZone.GetCategoryNext(iCategorySeqIndex);
    }

    if (bRes == false)
        return INGAMESHOP_ERROR_INVALID_INDEX;

    return iCategorySeqIndex;
}

void CInGameShopSystem::SetCategoryName()
{
    m_listCategoryName.clear();

    int iCategorySeqIndex;
    CShopCategory Category;
    m_SelectedZone.SetCategoryFirst();

    while (m_SelectedZone.GetCategoryNext(iCategorySeqIndex))
    {
        m_pCategoryList->GetValueByKey(iCategorySeqIndex, Category);
        m_listCategoryName.push_back(Category.CategroyName);
    }
}

void CInGameShopSystem::ShopOpenLock()
{
    m_bIsShopOpenLock = true;
}

void CInGameShopSystem::ShopOpenUnLock()
{
    m_bIsShopOpenLock = false;
}

CListVersionInfo CInGameShopSystem::GetScriptVer()
{
    return m_ScriptVerInfo;
}

CListVersionInfo CInGameShopSystem::GetBannerVer()
{
    return m_BannerVerInfo;
}

#ifdef KJH_MOD_SHOP_SCRIPT_DOWNLOAD
CListVersionInfo CInGameShopSystem::GetCurrentScriptVer()
{
    return m_CurrentScriptVerInfo;
}

CListVersionInfo CInGameShopSystem::GetCurrentBannerVer()
{
    return m_CurrentBannerVerInfo;
}
#endif // KJH_MOD_SHOP_SCRIPT_DOWNLOAD
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
#ifdef _WIN32
#pragma comment(lib, "Urlmon.lib")
#endif

CBannerInfo::CBannerInfo() // OK
{
    memset(this->BannerName, 0, sizeof(this->BannerName));
    memset(this->BannerImageURL, 0, sizeof(this->BannerImageURL));
    memset(this->BannerImagePath, 0, sizeof(this->BannerImagePath));
    memset(this->BannerLinkURL, 0, sizeof(this->BannerLinkURL));
}
CBannerInfo::~CBannerInfo() // OK
{
}

bool CBannerInfo::SetBanner(std::wstring strdata, std::wstring strDirPath, bool bDonwLoad) // OK
{
    if (strdata.empty())
        return 0;

    CStringToken Token(strdata, L"@");

    if (Token.hasMoreTokens() == 0)
        return 0;

    this->BannerSeq = _wtoi(Token.nextToken().c_str());

    StringCchCopy(this->BannerName, std::size(this->BannerName), Token.nextToken().c_str());

    StringCchCopy(this->BannerImageURL, std::size(this->BannerImageURL), Token.nextToken().c_str());

    this->BannerOrder = _wtoi(Token.nextToken().c_str());
    this->BannerDirection = _wtoi(Token.nextToken().c_str());

    CStringMethod::ConvertStringToDateTime(this->BannerStartDate, Token.nextToken());
    CStringMethod::ConvertStringToDateTime(this->BannerEndDate, Token.nextToken());

    StringCchCopy(this->BannerLinkURL, std::size(this->BannerLinkURL), Token.nextToken().c_str());

    std::wstring url = this->BannerImageURL;
    std::size_t pos = url.rfind(L"/", std::wstring::npos);

    if (pos != std::wstring::npos)
    {
        std::wstring sub = url.substr(pos + 1, url.length() - pos - 1);

        StringCchPrintf(this->BannerImagePath, std::size(this->BannerImagePath), L"%ls%ls",
                        strDirPath.c_str(), sub.c_str());

        if (bDonwLoad || GetFileAttributes(this->BannerImagePath) == INVALID_FILE_ATTRIBUTES)
        {
#ifdef _WIN32
            URLDownloadToFile(0, this->BannerImageURL, this->BannerImagePath, 0, 0);
#else
            // No portable downloader yet (issue #462); the banner simply stays
            // absent and the shop renders without it.
#endif
        }
    }

    return 1;
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CBannerInfoList::CBannerInfoList() // OK
{
    this->Clear();
}

CBannerInfoList::~CBannerInfoList() // OK
{
}

WZResult CBannerInfoList::LoadBanner(std::wstring strDirPath, std::wstring strScriptFileName,
                                     bool bDonwLoad)
{
    WZResult result;

    result.BuildSuccessResult();

    std::wifstream ifs;

    std::wstring path = strDirPath + strScriptFileName;

    ifs.open(std::filesystem::path(path), std::ifstream::in);

    if (ifs.is_open())
    {
        this->Clear();

        wchar_t buff[1024] = {0};

        while (true)
        {
            if (!ifs.getline(buff, sizeof(buff)))
                break;

            CBannerInfo info;

            if (info.SetBanner(buff, strDirPath, bDonwLoad))
            {
                this->Append(info);
            }
        }

        ifs.close();
    }
    else
    {
        result.SetResult(6, GetLastError(), L"Banner file open fail");
    }

    return result;
}

void CBannerInfoList::Clear() // OK
{
    this->m_BannerInfos.clear();
}

int CBannerInfoList::GetSize() // OK
{
    return this->m_BannerInfos.size();
}

void CBannerInfoList::Append(CBannerInfo banner) // OK
{
    this->m_BannerInfos.insert(std::make_pair(banner.BannerSeq, banner));
}

void CBannerInfoList::SetFirst() // OK
{
    this->m_BannerInfoIter = this->m_BannerInfos.begin();
}
bool CBannerInfoList::GetNext(CBannerInfo &banner) // OK
{
    if (this->m_BannerInfoIter == this->m_BannerInfos.end())
        return 0;

    banner = this->m_BannerInfoIter->second;

    this->m_BannerInfoIter++;
    return 1;
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CBannerListManager::CBannerListManager() // OK
{
    this->m_BannerInfoList = new CBannerInfoList();

    this->m_vScriptFiles.push_back(BANNER_SCRIPT_FILENAME);
}

CBannerListManager::~CBannerListManager() // OK
{
    SAFE_DELETE(m_BannerInfoList);
}

WZResult CBannerListManager::LoadScript(bool bDonwLoad) // OK
{
    std::wstring path = this->GetScriptPath();

    return this->m_BannerInfoList->LoadBanner(path, BANNER_SCRIPT_FILENAME, bDonwLoad);
}

#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifndef _WIN32

namespace
{
std::wstring BuildFtpUrl(const std::wstring &host, unsigned short port,
                         const std::wstring &remotePath)
{
    // WinINet's FtpOpenFile treated the remote path as relative to the login
    // directory; libcurl's ftp://host/path does the same with a single leading
    // slash. Normalise separators and drop any leading slash so exactly one
    // sits between the host and the path.
    std::wstring path = remotePath;
    for (wchar_t &ch : path)
    {
        if (ch == L'\\')
        {
            ch = L'/';
        }
    }
    while (!path.empty() && path.front() == L'/')
    {
        path.erase(path.begin());
    }

    return L"ftp://" + host + L":" + std::to_wstring(port) + L"/" + path;
}
} // namespace
#endif // _WIN32

CFTPFileDownLoader::CFTPFileDownLoader() // OK
{
    this->m_Break = 0;
    this->m_pFileDownloader = NULL;
}

CFTPFileDownLoader::~CFTPFileDownLoader() // OK
{
#ifdef _WIN32
    // The WinInet FileDownloader is excluded off Windows (issue #462); the
    // pointer is always null there, and even a delete of a null pointer would
    // still link against the destructor symbol.
    SAFE_DELETE(this->m_pFileDownloader);
#endif
}

WZResult CFTPFileDownLoader::DownLoadFiles(DownloaderType type, std::wstring strServerIP,
                                           unsigned short PortNum, std::wstring strUserName,
                                           std::wstring strPWD, std::wstring strRemotepath,
                                           std::wstring strlocalpath, bool bPassiveMode,
                                           CListVersionInfo Version,
                                           std::vector<std::wstring> vScriptFiles) // OK
{
    WZResult result;

#ifndef _WIN32
    // Off Windows the WinINet FileDownloader is replaced by libcurl (issue #462).
    result.BuildSuccessResult();

    wchar_t versionDir[MAX_PATH] = {0};
    StringCchPrintf(versionDir, std::size(versionDir), L"%03d.%04d.%03d", Version.Zone,
                    Version.year, Version.yearId);

    const std::wstring remoteBase = strRemotepath + versionDir + L"/";
    const std::wstring localBase = strlocalpath + versionDir + L"/";

    for (std::vector<std::wstring>::iterator it = vScriptFiles.begin(); it != vScriptFiles.end();
         ++it)
    {
        const std::wstring localPath = localBase + (*it);
        const std::wstring url = BuildFtpUrl(strServerIP, PortNum, remoteBase + (*it));

        result = CurlFileDownloader::DownloadFile(url, localPath, strUserName, strPWD, bPassiveMode,
                                                  &this->m_Break);

        if (this->m_Break != 0)
        {
            result.SetResult(1, 0, L"Time Out Break");
            break;
        }

        if (!result.IsSuccess())
        {
            break;
        }
    }

    return result;
#else
    result.BuildSuccessResult();

    DownloadServerInfo ServerInfo;
    DownloadFileInfo FileInfo;

    ServerInfo.SetPassiveMode(bPassiveMode);
    ServerInfo.SetOverWrite(1);
    ServerInfo.SetDownloaderType(FTP);
    ServerInfo.SetConnectTimeout(3000);
    ServerInfo.SetServerInfo((TCHAR *)strServerIP.c_str(), PortNum, (TCHAR *)strUserName.c_str(),
                             (TCHAR *)strPWD.c_str());

    wchar_t Buffer[MAX_PATH] = {0};

    StringCchPrintf(Buffer, std::size(Buffer), L"%03d.%04d.%03d", Version.Zone, Version.year,
                    Version.yearId);

    strRemotepath += Buffer;
    strRemotepath += L"/";
    strlocalpath += Buffer;
    strlocalpath += L"\\";

    for (std::vector<std::wstring>::iterator it = vScriptFiles.begin(); it != vScriptFiles.end();
         it++)
    {
        std::wstring lPath = strlocalpath + (*it);
        std::wstring rPath = strRemotepath + (*it);

        FileInfo.SetFilePath((TCHAR *)it->c_str(), (TCHAR *)lPath.c_str(), (TCHAR *)rPath.c_str(),
                             NULL);

        this->m_pFileDownloader = new FileDownloader(NULL, &ServerInfo, &FileInfo);

        result = this->m_pFileDownloader->DownloadFile();

        SAFE_DELETE(this->m_pFileDownloader);

        if (this->m_Break != 0)
        {
            result.SetResult(1, 0, L"Time Out Break");
            break;
        }

        if (!result.IsSuccess())
        {
            break;
        }
    }

    return result;
#endif // _WIN32
}

void CFTPFileDownLoader::Break() // OK
{
    this->m_Break = 1;
#ifdef _WIN32
    if (this->m_pFileDownloader != NULL)
        m_pFileDownloader->Break();
#endif
}

BOOL CFTPFileDownLoader::CreateFolder(std::wstring strFilePath) // OK
{
    if (GetFileAttributes(strFilePath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        return CreateDirectory(strFilePath.c_str(), 0);
    }

    return 1;
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
CListManager::CListManager() // OK
{
    this->m_pFTPDownLoader = NULL;
    this->m_ListManagerInfo.m_strServerIP.clear();
    this->m_ListManagerInfo.m_strUserID.clear();
    this->m_ListManagerInfo.m_strPWD.clear();
    this->m_ListManagerInfo.m_strRemotePath.clear();
    this->m_ListManagerInfo.m_strLocalPath.clear();
    this->m_vScriptFiles.clear();
}

CListManager::~CListManager() // OK
{
    SAFE_DELETE(m_pFTPDownLoader);
}

void CListManager::SetListManagerInfo(DownloaderType type, const wchar_t *ServerIP,
                                      const wchar_t *UserID, const wchar_t *Pwd,
                                      const wchar_t *RemotePath, const wchar_t *LocalPath,
                                      CListVersionInfo Version, DWORD dwDownloadMaxTime)
{
    unsigned short port = 80;

    if (type == FTP)
        port = 21;

    this->SetListManagerInfo(type, ServerIP, port, UserID, Pwd, RemotePath, LocalPath,
                             FTP_MODE_ACTIVE, Version, dwDownloadMaxTime);
}

void CListManager::SetListManagerInfo(DownloaderType type, const wchar_t *ServerIP,
                                      unsigned short PortNum, const wchar_t *UserID,
                                      const wchar_t *Pwd, const wchar_t *RemotePath,
                                      const wchar_t *LocalPath, FTP_SERVICE_MODE ftpMode,
                                      CListVersionInfo Version,
                                      DWORD dwDownloadMaxTime) // OK
{
    this->m_ListManagerInfo.m_DownloaderType = type;
    this->m_ListManagerInfo.m_nPortNum = PortNum;
    this->m_ListManagerInfo.m_ftpMode = ftpMode;
    this->m_ListManagerInfo.m_strLocalPath = LocalPath;
    this->m_ListManagerInfo.m_strRemotePath = RemotePath;
    this->m_ListManagerInfo.m_strPWD = Pwd;
    this->m_ListManagerInfo.m_strServerIP = ServerIP;
    this->m_ListManagerInfo.m_strUserID = UserID;
    this->m_ListManagerInfo.m_Version = Version;
    this->m_ListManagerInfo.m_dwDownloadMaxTime = dwDownloadMaxTime;

    if (GetFileAttributes(LocalPath) == INVALID_FILE_ATTRIBUTES)
        CreateDirectory(LocalPath, 0);

    if (this->m_ListManagerInfo.m_strLocalPath.substr(this->m_ListManagerInfo.m_strLocalPath.size(),
                                                      1) != L"\\")
    {
        this->m_ListManagerInfo.m_strLocalPath += L"\\";
    }

    if (this->m_ListManagerInfo.m_strRemotePath.substr(
            this->m_ListManagerInfo.m_strRemotePath.size(), 1) != L"/")
    {
        this->m_ListManagerInfo.m_strRemotePath += L"/";
    }
}

WZResult CListManager::LoadScriptList(bool bDonwLoad) // OK
{
    this->m_Result.BuildSuccessResult();

    if (!bDonwLoad && this->IsScriptFileExist())
    {
        goto JUMP;
    }

    this->m_Result = this->FileDownLoad();

    if (!this->m_Result.IsSuccess())
    {
        this->DeleteScriptFiles();
        return this->m_Result;
    }

    if (this->IsScriptFileExist())
    {
    JUMP:
        this->m_Result = this->LoadScript(bDonwLoad);

        if (!this->m_Result.IsSuccess())
            this->DeleteScriptFiles();
    }
    else
    {
        this->DeleteScriptFiles();

        this->m_Result.SetResult(2, 0, L"File Size Zero");
    }

    return this->m_Result;
}

WZResult CListManager::LoadScriptList(bool bDonwLoad, CmuConsoleDebug &consoleDebug)
{
    this->m_Result.BuildSuccessResult();

    if (!bDonwLoad && this->IsScriptFileExist())
    {
        goto JUMP;
    }

    this->m_Result = this->FileDownLoad();

    if (!this->m_Result.IsSuccess())
    {
        this->DeleteScriptFiles();
        return this->m_Result;
    }

    if (this->IsScriptFileExist())
    {
    JUMP:
        this->m_Result = this->LoadScript(bDonwLoad, consoleDebug);

        if (!this->m_Result.IsSuccess())
            this->DeleteScriptFiles();
    }
    else
    {
        this->DeleteScriptFiles();
        this->m_Result.SetResult(2, 0, L"File Size Zero");
    }

    return this->m_Result;
}

WZResult CListManager::LoadScript(bool bDonwLoad, CmuConsoleDebug &consoleDebug)
{
    (void)consoleDebug;
    return LoadScript(bDonwLoad);
}

bool CListManager::IsScriptFileExist() // OK
{
    std::wstring path = this->GetScriptPath();

    for (std::vector<std::wstring>::iterator it = this->m_vScriptFiles.begin();
         it != this->m_vScriptFiles.end(); it++)
    {
        std::wstring file_path = path + *(it);

        if (GetFileAttributes(file_path.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            return 0;
        }
    }

    return 1;
}

std::wstring CListManager::GetScriptPath() // OK
{
    TCHAR buff[MAX_PATH] = {0};

    StringCchPrintf(buff, std::size(buff), L"%03d.%04d.%03d", m_ListManagerInfo.m_Version.Zone,
                    m_ListManagerInfo.m_Version.year, m_ListManagerInfo.m_Version.yearId);

    std::wstring path = this->m_ListManagerInfo.m_strLocalPath;
    path += buff;
    path += L"\\";

    return path;
}

void CListManager::DeleteScriptFiles() // OK
{
    std::wstring path = this->GetScriptPath();

    for (std::vector<std::wstring>::iterator it = this->m_vScriptFiles.begin();
         it != this->m_vScriptFiles.end(); it++)
    {
        std::wstring file_path = path + (*it);

        DeleteFile(file_path.c_str());
    }
}

WZResult CListManager::FileDownLoad() // OK
{
#ifndef _WIN32
    // The watchdog thread guards a WinInet download that is excluded off
    // Windows (issue #462); DownLoadFiles fails immediately there, so run it
    // inline.
    this->m_Result = this->FileDownLoadImpl();
#else
    if (this->m_ListManagerInfo.m_dwDownloadMaxTime > 0)
    {
        unsigned int ThreadID = 0;

        auto hHandle =
            (HANDLE)_beginthreadex(0, 0, CListManager::RunFileDownLoadThread, this, 0, &ThreadID);

        if (hHandle == INVALID_HANDLE_VALUE)
        {
            this->m_Result.BuildResult(8, GetLastError(), L"Fail : _beginthreadex");
        }
        else if (WaitForSingleObject(hHandle, this->m_ListManagerInfo.m_dwDownloadMaxTime) ==
                 WAIT_TIMEOUT)
        {
            if (this->m_pFTPDownLoader != NULL)
                this->m_pFTPDownLoader->Break();

            WaitForSingleObject(hHandle, INFINITE);

            if (m_pFTPDownLoader != NULL)
                if (m_pFTPDownLoader->GetFileDownloader() != NULL)
                    m_pFTPDownLoader->GetFileDownloader()->Break();

            SAFE_DELETE(m_pFTPDownLoader);

            CloseHandle(hHandle);

            this->m_Result.BuildResult(1, 0, L"Time Out!");
        }
        else
        {
            CloseHandle(hHandle);
        }
    }
    else
    {
        this->m_Result = this->FileDownLoadImpl();
    }
#endif // _WIN32

    return this->m_Result;
}

WZResult CListManager::FileDownLoadImpl() // OK
{
    if (m_pFTPDownLoader != NULL)
    {
        m_pFTPDownLoader->Break();

#ifdef _WIN32
        if (m_pFTPDownLoader->GetFileDownloader() != NULL)
        {
            m_pFTPDownLoader->GetFileDownloader()->Break();
        }
#endif
    }

    SAFE_DELETE(m_pFTPDownLoader); // FIX THIS

    this->m_pFTPDownLoader = new CFTPFileDownLoader;

    this->m_Result = this->m_pFTPDownLoader->DownLoadFiles(
        this->m_ListManagerInfo.m_DownloaderType, this->m_ListManagerInfo.m_strServerIP,
        this->m_ListManagerInfo.m_nPortNum, this->m_ListManagerInfo.m_strUserID,
        this->m_ListManagerInfo.m_strPWD, this->m_ListManagerInfo.m_strRemotePath,
        this->m_ListManagerInfo.m_strLocalPath,
        this->m_ListManagerInfo.m_ftpMode == FTP_MODE_PASSIVE, this->m_ListManagerInfo.m_Version,
        this->m_vScriptFiles);

    return this->m_Result;
}

unsigned int __stdcall CListManager::RunFileDownLoadThread(LPVOID pParam) // OK
{
    auto *p = reinterpret_cast<CListManager *>(pParam);

    p->m_Result = p->FileDownLoadImpl();

    return 0;
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CShopCategory::CShopCategory() // OK
{
    this->CategoryList.clear();
    this->PackageList.clear();
}

CShopCategory::~CShopCategory() // OK
{
}

bool CShopCategory::SetCategory(std::wstring strdata) // OK
{
    if (strdata.empty())
        return 0;

    CStringToken token(strdata, L"@");

    if (token.hasMoreTokens() == 0)
        return 0;

    this->ProductDisplaySeq = _wtoi(token.nextToken().c_str());
    StringCchCopy(this->CategroyName, std::size(this->CategroyName), token.nextToken().c_str());
    this->EventFlag = _wtoi(token.nextToken().c_str());
    this->OpenFlag = _wtoi(token.nextToken().c_str());
    this->ParentProductDisplaySeq = _wtoi(token.nextToken().c_str());
    this->DisplayOrder = _wtoi(token.nextToken().c_str());
    this->Root = _wtoi(token.nextToken().c_str());

    return 1;
}

void CShopCategory::SetCategoryFirst() // OK
{
    this->Categoryiter = this->CategoryList.begin();
}

bool CShopCategory::GetCategoryNext(int &CategorySeq) // OK
{
    if (this->Categoryiter == this->CategoryList.end())
        return 0;

    CategorySeq = (*this->Categoryiter);
    this->Categoryiter++;
    return 1;
}

void CShopCategory::SetPackagSeqFirst() // OK
{
    this->Packageiter = this->PackageList.begin();
}

bool CShopCategory::GetPackagSeqNext(int &PackagSeq) // OK
{
    if (this->Packageiter == this->PackageList.end())
        return 0;

    PackagSeq = (*this->Packageiter);
    this->Packageiter++;
    return 1;
}

void CShopCategory::AddPackageSeq(int PackageSeq) // OK
{
    this->PackageList.push_back(PackageSeq);
}

void CShopCategory::ClearPackageSeq()
{
    this->PackageList.clear();
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CShopCategoryList::CShopCategoryList() // OK
{
    this->Clear();
}

CShopCategoryList::~CShopCategoryList() // OK
{
}

void CShopCategoryList::Clear() // OK
{
    this->m_Categroys.clear();
    this->m_CategoryIndex.clear();
}

int CShopCategoryList::GetSize() // OK
{
    return static_cast<int>(this->m_Categroys.size());
}

void CShopCategoryList::Append(CShopCategory category) // OK
{
    if (this->m_Categroys.find(category.ProductDisplaySeq) != this->m_Categroys.end())
        return;

    this->m_Categroys.insert(std::make_pair(category.ProductDisplaySeq, category));

    this->m_CategoryIndex.push_back(category.ProductDisplaySeq);

    if (category.Root != 1)
    {
        auto it = this->m_Categroys.find(category.ParentProductDisplaySeq);

        if (it == this->m_Categroys.end())
        {
            return;
        }

        it->second.CategoryList.push_back(category.ProductDisplaySeq);
    }
}

void CShopCategoryList::SetFirst() // OK
{
    this->m_Categoryiter = this->m_Categroys.begin();
}

bool CShopCategoryList::GetNext(CShopCategory &category) // OK
{
    if (this->m_Categoryiter == this->m_Categroys.end())
        return 0;

    category = this->m_Categoryiter->second;
    this->m_Categoryiter++;
    return 1;
}

bool CShopCategoryList::GetValueByKey(int nKey, CShopCategory &category) // OK
{
    auto it = this->m_Categroys.find(nKey);

    if (it == this->m_Categroys.end())
    {
        return 0;
    }

    category = it->second;
    return 1;
}

bool CShopCategoryList::GetValueByIndex(int nIndex, CShopCategory &category) // OK
{
    if (nIndex < 0 || nIndex >= static_cast<int>(this->m_CategoryIndex.size()))
    {
        return 0;
    }

    return this->GetValueByKey(this->m_CategoryIndex[nIndex], category);
}

bool CShopCategoryList::InsertPackage(int Category, int Package) // OK
{
    auto it = this->m_Categroys.find(Category);

    if (it == this->m_Categroys.end())
    {
        return 0;
    }

    it->second.AddPackageSeq(Package);
    return 1;
}

bool CShopCategoryList::RefreshPackageSeq(int Category, int PackageSeqs[], int PackageCount) // OK
{
    auto it = this->m_Categroys.find(Category);

    if (it == this->m_Categroys.end())
    {
        return 0;
    }

    it->second.ClearPackageSeq();

    for (int n = 0; n < PackageCount; n++)
    {
        it->second.AddPackageSeq(PackageSeqs[n]);
    }

    return 1;
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CShopList::CShopList(SessionKeeper &keeper) // OK
    : SessionLegacyCalls(keeper), g_ConsoleDebug(keeper.ConsoleDebug())
{
    this->m_CategoryListPtr = new CShopCategoryList;
    this->m_PackageListPtr = new CShopPackageList;
    this->m_ProductListPtr = new CShopProductList;
}

CShopList::~CShopList() // OK
{
    SAFE_DELETE(m_CategoryListPtr);
    SAFE_DELETE(m_PackageListPtr);
    SAFE_DELETE(m_ProductListPtr);
}

WZResult CShopList::LoadCategroy(const wchar_t *szFilePath) // OK
{
    WZResult result;

    FILE_ENCODE enc = this->IsFileEncodingUtf8(szFilePath);

    std::ifstream ifs;

    ifs.open(std::filesystem::path(szFilePath), std::ifstream::in);

    DWORD LastError = GetLastError();

    for (int n = 0; !ifs.is_open() && n < 10; ++n)
    {
        Sleep(0x64);
        ifs.open(std::filesystem::path(szFilePath), std::ifstream::in);
        LastError = GetLastError();
    }

    char buff[1024] = {0};

    if (ifs.is_open())
    {
        this->GetCategoryListPtr()->Clear();

        int linesRead = 0;
        int rowsParsed = 0;

        while (true)
        {
            memset(buff, 0, sizeof(buff));

            if (!ifs.getline(buff, sizeof(buff)))
                break;

            ++linesRead;

            CShopCategory cat;

            const std::wstring data = this->GetDecodedString(buff, enc);

            if (cat.SetCategory(data))
            {
                this->GetCategoryListPtr()->Append(cat);
                ++rowsParsed;
            }
        }

        ifs.close();

        // Make a zero-row decode impossible to mistake for success.
        // If the decoder ever mangles the catalog again, rowsParsed=0 (with a
        // non-zero linesRead) will be obvious in muConsoleDebug instead of
        // silently producing an empty shop grid with no error dialog.
        g_ConsoleDebug.Write(
            MCD_NORMAL,
            L"[XShop] LoadCategroy: parsed %d category rows from %d lines (encode=%d) <%ls>",
            rowsParsed, linesRead, (int)enc, szFilePath);
    }
    else
    {
        result.SetResult(PT_LOADLIBRARY, LastError, L"package file open fail");
    }

    return result;
}

WZResult CShopList::LoadPackage(const wchar_t *szFilePath) // OK
{
    WZResult result;

    FILE_ENCODE enc = this->IsFileEncodingUtf8(szFilePath);

    std::ifstream ifs;

    ifs.open(std::filesystem::path(szFilePath), std::ifstream::in);

    DWORD LastError = GetLastError();

    for (int n = 0; !ifs.is_open() && n < 10; ++n)
    {
        Sleep(0x64);
        ifs.open(std::filesystem::path(szFilePath), std::ifstream::in);
        LastError = GetLastError();
    }

    char buff[1024] = {0};

    if (ifs.is_open())
    {
        this->GetPackageListPtr()->Clear();

        while (true)
        {
            if (!ifs.getline(buff, sizeof(buff)))
                break;

            CShopPackage pack;

            if (pack.SetPackage(this->GetDecodedString(buff, enc)))
            {
                this->GetPackageListPtr()->Append(pack);
                this->GetCategoryListPtr()->InsertPackage(pack.ProductDisplaySeq,
                                                          pack.PackageProductSeq);
            }
        }

        ifs.close();
    }
    else
    {
        result.SetResult(4, LastError, L"package file open fail");
    }

    return result;
}

WZResult CShopList::LoadProduct(const wchar_t *szFilePath) // OK
{
    WZResult result;

    result.BuildSuccessResult();

    FILE_ENCODE enc = this->IsFileEncodingUtf8(szFilePath);

    std::ifstream ifs;

    ifs.open(std::filesystem::path(szFilePath), std::ifstream::in);

    DWORD LastError = GetLastError();

    for (int n = 0; !ifs.is_open() && n < 10; ++n)
    {
        Sleep(0x64);
        ifs.open(std::filesystem::path(szFilePath), std::ifstream::in);
        LastError = GetLastError();
    }

    char buff[1024] = {0};

    if (ifs.is_open())
    {
        this->GetProductListPtr()->Clear();

        while (true)
        {
            memset(buff, 0, sizeof(buff));

            if (!ifs.getline(buff, sizeof(buff)))
                break;

            CShopProduct product;

            std::wstring data = this->GetDecodedString(buff, enc);

            if (product.SetProduct(data))
            {
                this->GetProductListPtr()->Append(product);
            }
        }

        ifs.close();
    }
    else
    {
        result.SetResult(4, LastError, L"package file open fail");
    }

    return result;
}

void CShopList::SetCategoryListPtr(CShopCategoryList *CategoryListPtr) // OK
{
    m_CategoryListPtr = CategoryListPtr;
}

void CShopList::SetPackageListPtr(CShopPackageList *PackagePtr) // OK
{
    m_PackageListPtr = PackagePtr;
}

void CShopList::SetProductListPtr(CShopProductList *ProductListPtr) // OK
{
    m_ProductListPtr = ProductListPtr;
}

FILE_ENCODE CShopList::IsFileEncodingUtf8(const wchar_t *szFilePath) // OK
{
    std::ifstream ifs;

    ifs.open(std::filesystem::path(szFilePath), std::ifstream::in);

    if (!ifs.is_open())
    {
        return FE_ANSI;
    }

    char buff[16] = {0};

    ifs.getline(buff, sizeof(buff));

    ifs.close();

    if (strlen(buff) < 3)
    {
        return FE_ANSI;
    }

    if (buff[0] == 0xEF && buff[1] == 0xBB && buff[2] == 0xBF)
    {
        return FE_UTF8;
    }

    if (buff[0] == 0xFF && buff[1] == 0xFE)
    {
        return FE_UNICODE;
    }

    return FE_ANSI;
}

std::wstring CShopList::GetDecodedString(const char *buffer, FILE_ENCODE encode) // OK
{
    std::wstring result;

    if (encode == FE_UNICODE)
    {
        // UTF-16LE input: std::ifstream::getline read the raw little-endian bytes
        // into a narrow buffer, so they already are wide characters. Reinterpret
        // is the correct decode here (catalog files are ANSI today, so this path
        // is not normally exercised, but keep it lossless rather than empty).
        result = reinterpret_cast<const wchar_t *>(buffer);
        return result;
    }

    // FE_ANSI -> CP_ACP, FE_UTF8 -> CP_UTF8. Convert the narrow bytes to UTF-16
    // properly. The old decompiled code reinterpret-cast the narrow ASCII bytes
    // as wchar_t* ("todo: check if that's correct"), which turned a row like
    // "10@Item@200@..." into a single garbage token with no wide '@'
    // delimiters. CStringToken then yielded 1 field instead of 7, so every row
    // decoded to Root=0 -> zero category zones -> no tabs and an empty grid.
    const UINT codePage = (encode == FE_UTF8) ? CP_UTF8 : CP_ACP;

    int cchWideChar = MultiByteToWideChar(codePage, 0, buffer, -1, 0, 0);
    if (cchWideChar <= 0)
    {
        return result; // empty string on conversion failure
    }

    auto lpWideCharStr = new WCHAR[cchWideChar];
    MultiByteToWideChar(codePage, 0, buffer, -1, lpWideCharStr, cchWideChar);

    result = lpWideCharStr;

    delete[] lpWideCharStr;

    return result;
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CShopListManager::CShopListManager(SessionKeeper &keeper) // OK
    : SessionLegacyCalls(keeper)
{
    this->m_ShopList = new CShopList(keeper);

    this->m_vScriptFiles.push_back(SHOPLIST_SCRIPT_CATEGORY);
    this->m_vScriptFiles.push_back(SHOPLIST_SCRIPT_PACKAGE);
    this->m_vScriptFiles.push_back(SHOPLIST_SCRIPT_PRODUCT);
}

CShopListManager::~CShopListManager() // OK
{
    SAFE_DELETE(m_ShopList);
}

WZResult CShopListManager::LoadScript(bool bDonwLoad) // OK
{
    if (this->m_ShopList)
    {
        std::wstring path = this->GetScriptPath();

        this->m_Result =
            this->m_ShopList->LoadCategroy(std::wstring(path + SHOPLIST_SCRIPT_CATEGORY).c_str());

        if (this->m_Result.IsSuccess())
        {
            this->m_Result =
                this->m_ShopList->LoadPackage(std::wstring(path + SHOPLIST_SCRIPT_PACKAGE).c_str());

            if (this->m_Result.IsSuccess())
            {
                this->m_Result = this->m_ShopList->LoadProduct(
                    std::wstring(path + SHOPLIST_SCRIPT_PRODUCT).c_str());
            }
        }
    }
    else
    {
        this->m_Result.SetResult(PT_NO_INFO, 0, L"[CShopListManager::LoadScript] Failed");
    }

    return this->m_Result;
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CShopPackage::CShopPackage() // OK
{
    this->LeftCount = -1;
    this->ProductSeqList.clear();
    this->PriceSeqList.clear();
}
CShopPackage::~CShopPackage() // OK
{
}

bool CShopPackage::SetPackage(std::wstring strdata) // OK
{
    if (strdata.empty())
        return 0;

    CStringToken token(strdata, L"@");

    if (token.hasMoreTokens() == 0)
        return 0;

    this->ProductDisplaySeq = _wtoi(token.nextToken().c_str());
    this->ViewOrder = _wtoi(token.nextToken().c_str());
    this->PackageProductSeq = _wtoi(token.nextToken().c_str());
    StringCchCopy(this->PackageProductName, std::size(this->PackageProductName),
                  token.nextToken().c_str());
    this->PackageProductType = _wtoi(token.nextToken().c_str());
    this->Price = _wtoi(token.nextToken().c_str());
    StringCchCopy(this->Description, std::size(this->Description), token.nextToken().c_str());
    StringCchCopy(this->Caution, std::size(this->Caution), token.nextToken().c_str());
    this->SalesFlag = _wtoi(token.nextToken().c_str());
    this->GiftFlag = _wtoi(token.nextToken().c_str());
    CStringMethod::ConvertStringToDateTime(this->StartDate, token.nextToken());
    CStringMethod::ConvertStringToDateTime(this->EndDate, token.nextToken());
    this->CapsuleFlag = _wtoi(token.nextToken().c_str());
    this->CapsuleCount = _wtoi(token.nextToken().c_str());
    StringCchCopy(this->ProductCashName, std::size(this->ProductCashName),
                  token.nextToken().c_str());
    StringCchCopy(this->PricUnitName, std::size(this->PricUnitName), token.nextToken().c_str());
    this->DeleteFlag = _wtoi(token.nextToken().c_str());
    this->EventFlag = _wtoi(token.nextToken().c_str());
    this->ProductAmount = _wtoi(token.nextToken().c_str());
    this->SetProductSeqList(token.nextToken());
    StringCchCopy(this->InGamePackageID, std::size(this->InGamePackageID),
                  token.nextToken().c_str());
    this->ProductCashSeq = _wtoi(token.nextToken().c_str());
    this->PriceCount = _wtoi(token.nextToken().c_str());
    this->SetPriceSeqList(token.nextToken());
    this->DeductMileageFlag = _wtoi(token.nextToken().c_str()) != 0;
    this->CashType = _wtoi(token.nextToken().c_str());
    this->CashTypeFlag = _wtoi(token.nextToken().c_str());

    return 1;
}

void CShopPackage::SetLeftCount(int nCount) // OK
{
    this->LeftCount = nCount;
}

int CShopPackage::GetProductCount() // OK
{
    return static_cast<int>(this->ProductSeqList.size());
}

void CShopPackage::SetProductSeqFirst() // OK
{
    this->ProductSeqIter = this->ProductSeqList.begin();
}

bool CShopPackage::GetProductSeqFirst(int &ProductSeq) // OK
{
    this->ProductSeqIter = this->ProductSeqList.begin();

    if (this->ProductSeqIter == this->ProductSeqList.end())
        return 0;
    ProductSeq = (*this->ProductSeqIter);
    this->ProductSeqIter++;
    return 1;
}

bool CShopPackage::GetProductSeqNext(int &ProductSeq) // OK
{
    if (this->ProductSeqIter == this->ProductSeqList.end())
        return 0;
    ProductSeq = (*this->ProductSeqIter);
    this->ProductSeqIter++;
    return 1;
}

int CShopPackage::GetPriceCount()
{
    return static_cast<int>(this->PriceSeqList.size());
}

void CShopPackage::SetPriceSeqFirst()
{
    this->PriceSeqIter = this->PriceSeqList.begin();
}

bool CShopPackage::GetPriceSeqFirst(int &PriceSeq) // OK
{
    this->PriceSeqIter = this->PriceSeqList.begin();

    if (this->PriceSeqIter == this->PriceSeqList.end())
        return 0;
    PriceSeq = (*this->PriceSeqIter);
    this->PriceSeqIter++;
    return 1;
}

bool CShopPackage::GetPriceSeqNext(int &PriceSeq) // OK
{
    if (this->PriceSeqIter == this->PriceSeqList.end())
        return 0;
    PriceSeq = (*this->PriceSeqIter);
    this->PriceSeqIter++;
    return 1;
}

void CShopPackage::SetProductSeqList(std::wstring strdata) // OK
{
    CStringToken token(strdata, L"|");

    while (true)
    {
        std::wstring data = token.nextToken();

        if (data.empty())
            break;

        this->ProductSeqList.push_back(_wtoi(data.c_str()));
    }
}

void CShopPackage::SetPriceSeqList(std::wstring strdata) // OK
{
    CStringToken token(strdata, L"|");

    while (true)
    {
        std::wstring data = token.nextToken();

        if (data.empty())
            break;

        this->PriceSeqList.push_back(_wtoi(data.c_str()));
    }
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CShopPackageList::CShopPackageList() // OK
{
    this->m_Packages.clear();
}

CShopPackageList::~CShopPackageList() // OK
{
}

int CShopPackageList::GetSize() // OK
{
    return static_cast<int>(this->m_Packages.size());
}

void CShopPackageList::Clear() // OK
{
    this->m_Packages.clear();
}

void CShopPackageList::Append(CShopPackage package) // OK
{
    this->m_Packages.insert(std::make_pair(package.PackageProductSeq, package));
}

void CShopPackageList::SetFirst() // OK
{
    this->m_iter = this->m_Packages.begin();
}

bool CShopPackageList::GetNext(CShopPackage &package) // OK
{
    if (this->m_iter == this->m_Packages.end())
        return 0;

    package = this->m_iter->second;
    this->m_iter++;
    return 1;
}

bool CShopPackageList::GetValueByKey(int nKey, CShopPackage &package) // OK
{
    auto it = this->m_Packages.find(nKey);

    if (it == this->m_Packages.end())
        return 0;

    package = it->second;

    return 1;
}

bool CShopPackageList::GetValueByIndex(int nIndex, CShopPackage &package) // OK
{
    if (nIndex < 0 || nIndex >= static_cast<int>(this->m_PackageIndex.size()))
    {
        return 0;
    }

    return this->GetValueByKey(this->m_PackageIndex[nIndex], package);
}

bool CShopPackageList::SetPacketLeftCount(int PackageSeq, int nCount) // OK
{
    auto it = this->m_Packages.find(PackageSeq);

    if (it == this->m_Packages.end())
        return 0;

    it->second.LeftCount = nCount;

    return 1;
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CShopProduct::CShopProduct() // OK
{
}
CShopProduct::~CShopProduct() // OK
{
}

bool CShopProduct::SetProduct(std::wstring strdata) // OK
{
    if (strdata.empty())
        return 0;

    CStringToken token(strdata, L"@");

    if (token.hasMoreTokens() == 0)
        return 0;

    this->ProductSeq = _wtoi(token.nextToken().c_str());
    StringCchCopy(this->ProductName, std::size(this->ProductName), token.nextToken().c_str());
    StringCchCopy(this->PropertyName, std::size(this->PropertyName), token.nextToken().c_str());
    StringCchCopy(this->Value, std::size(this->Value), token.nextToken().c_str());
    StringCchCopy(this->UnitName, std::size(this->UnitName), token.nextToken().c_str());
    this->Price = _wtoi(token.nextToken().c_str());
    this->PriceSeq = _wtoi(token.nextToken().c_str());
    this->PropertyType = _wtoi(token.nextToken().c_str());
    this->MustFlag = _wtoi(token.nextToken().c_str());
    this->vOrder = _wtoi(token.nextToken().c_str());
    this->DeleteFlag = _wtoi(token.nextToken().c_str());
    this->StorageGroup = _wtoi(token.nextToken().c_str());
    this->ShareFlag = _wtoi(token.nextToken().c_str());
    StringCchCopy(this->InGamePackageID, std::size(this->InGamePackageID),
                  token.nextToken().c_str());
    this->PropertySeq = _wtoi(token.nextToken().c_str());
    this->ProductType = _wtoi(token.nextToken().c_str());
    this->UnitType = _wtoi(token.nextToken().c_str());

    return 1;
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CShopProductList::CShopProductList() // OK
{
    this->PriceSeqKey = -1;
    this->m_Products.clear();
}
CShopProductList::~CShopProductList() // OK
{
}

void CShopProductList::Clear() // OK
{
    this->m_Products.clear();
}

int CShopProductList::GetSize() // OK
{
    return static_cast<int>(this->m_Products.size());
}

void CShopProductList::Append(CShopProduct product) // OK
{
    this->m_Products.insert(std::make_pair(product.ProductSeq, product));
}

void CShopProductList::SetFirst() // OK
{
    this->m_ProductIter = this->m_Products.begin();
}

bool CShopProductList::GetNext(CShopProduct &product) // OK
{
    if (this->m_ProductIter == this->m_Products.end())
        return 0;

    product = this->m_ProductIter->second;
    this->m_ProductIter++;

    return 1;
}

void CShopProductList::SetProductSeqFirst(int ProductSeq) // OK
{
    this->m_ProductRange = this->m_Products.equal_range(ProductSeq);

    this->m_ProductSeqIter = this->m_ProductRange.first;
}

bool CShopProductList::GetProductSeqNext(CShopProduct &product) // OK
{
    if (this->m_ProductSeqIter == this->m_ProductRange.second)
        return 0;

    product = this->m_ProductSeqIter->second;

    this->m_ProductSeqIter++;

    return 1;
}

void CShopProductList::SetPriceSeqFirst(int ProductSeq, int PriceSeq) // OK
{
    this->PriceSeqKey = PriceSeq;

    this->m_PriceRange = this->m_Products.equal_range(ProductSeq);

    this->m_PriceSeqIter = this->m_PriceRange.first;
}

bool CShopProductList::GetPriceSeqNext(CShopProduct &product) // OK
{
    if (this->m_PriceSeqIter == this->m_PriceRange.second)
        return 0;

    if (this->PriceSeqKey == this->m_PriceSeqIter->second.PriceSeq)
    {
        product = this->m_PriceSeqIter->second;
        this->m_PriceSeqIter++;
        return 1;
    }

    this->m_PriceSeqIter++;

    return this->GetPriceSeqNext(product);
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CStringMethod::CStringMethod() // OK
{
}

CStringMethod::~CStringMethod() // OK
{
}

void CStringMethod::ConvertStringToDateTime(tm &datetime, std::wstring strdata) // OK
{
    if (strdata.length() >= 4)
    {
        datetime.tm_year = _wtoi(strdata.substr(0, 4).c_str()) - 1900;
    }

    if (strdata.length() >= 6)
    {
        datetime.tm_mon = _wtoi(strdata.substr(4, 2).c_str()) - 1;
    }

    if (strdata.length() >= 8)
    {
        datetime.tm_mday = _wtoi(strdata.substr(6, 2).c_str());
    }

    if (strdata.length() >= 10)
    {
        datetime.tm_hour = _wtoi(strdata.substr(8, 2).c_str());
    }

    if (strdata.length() >= 12)
    {
        datetime.tm_min = _wtoi(strdata.substr(10, 2).c_str());
    }

    if (strdata.length() >= 14)
    {
        datetime.tm_sec = _wtoi(strdata.substr(12, 2).c_str());
    }

    mktime(&datetime);
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CStringToken::CStringToken() // OK
{
    this->data.clear();
    this->delimiter.clear();
    this->tokens.clear();
}

CStringToken::~CStringToken() // OK
{
}

CStringToken::CStringToken(const std::wstring &dataLine, const std::wstring &delim) // OK
{
    this->data = dataLine;
    this->delimiter = delim;
    this->tokens.clear();
    this->index = this->tokens.begin();
    this->split();
}

size_t CStringToken::countTokens() // OK
{
    return this->tokens.size();
}

bool CStringToken::hasMoreTokens()
{
    return this->index != this->tokens.end();
}

std::wstring CStringToken::nextToken() // OK
{
    std::wstring result;

    if (this->index == this->tokens.end())
    {
        result.assign(L"\0");
    }
    else
    {
        result.assign((*this->index));

        this->index++;
    }

    return result;
}

void CStringToken::split() // OK
{
    std::size_t first_not = this->data.find_first_not_of(this->delimiter, 0);
    std::size_t first = this->data.find_first_of(this->delimiter, first_not);

    while (first != std::wstring::npos || first_not != std::wstring::npos)
    {
        std::wstring subdata = this->data.substr(first_not, first - first_not);

        this->tokens.push_back(subdata);

        this->IsNullString(first);

        first_not = this->data.find_first_not_of(this->delimiter, first);
        first = this->data.find_first_of(this->delimiter, first_not);
    }

    this->index = this->tokens.begin();
}

void CStringToken::IsNullString(std::wstring::size_type pos) // OK
{
    std::wstring search = this->data.substr(pos + 1, this->delimiter.length());

    if (search == this->delimiter)
    {
        this->tokens.push_back(L"\0");

        this->IsNullString(pos + 1);
    }
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

TCHAR *Path::GetCurrentFullPath(TCHAR *szPath)
{
    if (!szPath)
        return 0;

    GetModuleFileName(0, szPath, MAX_PATH);

    return szPath;
}

TCHAR *Path::GetCurrentDirectory(TCHAR *szPath)
{
    if (!szPath)
        return 0;

    GetModuleFileName(0, szPath, MAX_PATH);

    wchar_t *chr = wcsrchr(szPath, '\\');

    if (!chr)
        return 0;

    (*chr) = 0;

    return szPath;
}

TCHAR *Path::GetCurrentFileName(TCHAR *szPath)
{
    if (!szPath)
        return 0;

    GetModuleFileName(0, szPath, MAX_PATH);

    return Path::GetFileName(szPath);
}

TCHAR *Path::SetDirString(TCHAR *szPath)
{
    if (!szPath)
        return 0;

    if ((*szPath))
    {
        std::size_t len = 0;
        StringCchLength(szPath, MAX_PATH, &len);

        if (szPath[len - 1] != '\\')
        {
            szPath[len] = '\\';
            szPath[len + 1] = 0;
        }
    }
    else
    {
        szPath[0] = '\\';
        szPath[1] = 0;
    }

    return szPath;
}

TCHAR *Path::ClearDirString(TCHAR *szPath)
{
    if (!szPath || !(*szPath))
        return szPath;

    std::size_t len = 0;
    StringCchLength(szPath, MAX_PATH, &len);

    if (len > 0 && (szPath[len - 1] == '\\' || szPath[len - 1] == '/' || szPath[len - 1] == '"'))
    {
        szPath[len - 1] = 0;
    }

    if (len >= 2 && (szPath[len - 2] == '\\' || szPath[len - 2] == '/' || szPath[len - 2] == '"'))
    {
        szPath[len - 2] = 0;
    }

    if (szPath[0] == '"')
    {
        StringCchCopy(szPath, MAX_PATH, szPath + 1);
    }

    return szPath;
}

TCHAR *Path::GetDirectory(TCHAR *szPath)
{
    if (!szPath || !(*szPath))
        return szPath;

    wchar_t *chr = wcsrchr(szPath, '\\');

    if (!chr)
        chr = wcsrchr(szPath, L'/');

    if (chr)
        (*chr) = 0;
    else
        StringCchCopy(szPath, MAX_PATH, L".");

    return szPath;
}

TCHAR *Path::GetFileName(TCHAR *szPath)
{
    if (!szPath || !(*szPath))
        return szPath;

    wchar_t *chr = wcsrchr(szPath, L'\\');

    if (!chr)
        chr = wcsrchr(szPath, L'/');

    if (chr)
        StringCchCopy(szPath, MAX_PATH, szPath + 1);

    return szPath;
}

TCHAR *Path::ChangeSlashToBackSlash(TCHAR *szPath)
{
    if (!szPath || !(*szPath))
        return szPath;

    std::size_t len = 0;
    StringCchLength(szPath, MAX_PATH, &len);

    for (std::size_t n = 0; n < len; n++)
    {
        if (szPath[n] == '\\')
            szPath[n] = '/';
    }

    return szPath;
}

TCHAR *Path::ChangeBackSlashToSlash(TCHAR *szPath)
{
    if (!szPath || !(*szPath))
        return szPath;

    std::size_t len = 0;
    StringCchLength(szPath, MAX_PATH, &len);

    for (std::size_t n = 0; n < len; n++)
    {
        if (szPath[n] == '/')
            szPath[n] = '\\';
    }

    return szPath;
}

BOOL Path::ReadFileLastLine(TCHAR *szFile, TCHAR *szLastLine)
{
    std::ifstream ifs(std::filesystem::path(szFile), std::ifstream::in | std::ifstream::binary);

    char buff[1024] = {0};

    if (szFile && szLastLine && *szFile && ifs.is_open())
    {
        std::size_t len = 0;

        while (!ifs.eof())
        {
            ifs.getline(buff, sizeof(buff));

            len = 0;
            StringCchLengthA(buff, sizeof(buff), &len);

            // TODO convert buff to utf8
        }

        ifs.close();

        len = 0;
        StringCchLength(szLastLine, 1024, &len);

        if (len > 1)
        {
            return 1;
        }
    }

    return 0;
}

BOOL Path::WriteNewFile(TCHAR *szFile, TCHAR *szText, INT nTextSize)
{
    if (!szFile || !szText)
        return 0;

    std::wfstream ofs(std::filesystem::path(szFile),
                      std::wfstream::out | std::wfstream::trunc | std::wfstream::binary);

    if (ofs.is_open())
    {
        ofs.seekp(0, std::ofstream::end);
        ofs.write(szText, nTextSize);
        ofs.close();

        return 1;
    }

    return 0;
}

BOOL Path::CreateDirectorys(TCHAR *szFilePath, BOOL bIsFile)

{
    if (!szFilePath || !(*szFilePath))
        return 0;

    wchar_t PathName[MAX_PATH] = {0};
    wchar_t buff2[MAX_PATH] = {0};

    StringCchCopy(PathName, std::size(PathName), szFilePath);

    if (bIsFile)
        Path::GetDirectory(PathName);

    if (CreateDirectory(PathName, 0) == 0)
    {
        StringCchCopy(buff2, std::size(buff2), PathName);

        wchar_t *chr1 = wcsrchr(buff2, L'\\');
        wchar_t *chr2 = wcschr(buff2, L'\\');

        if (!chr1 || !chr2 || chr1 == chr2)
            return 0;

        (*chr1) = 0;

        if (!Path::CreateDirectorys(buff2, 0))
            return 0;

        return CreateDirectory(PathName, 0);
    }

    return 0;
}
#endif

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

WZResult::WZResult() // OK
{
    this->SetSuccessResult();
}

WZResult::~WZResult() // OK
{
}

BOOL WZResult::IsSuccess() // OK
{
    return !this->m_dwErrorCode && !this->m_dwWindowErrorCode;
}

DWORD WZResult::GetErrorCode() // OK
{
    return this->m_dwErrorCode;
}

wchar_t *WZResult::GetErrorMessage() // OK
{
    return this->m_szErrorMessage;
}

DWORD WZResult::GetWindowErrorCode() // OK
{
    return this->m_dwWindowErrorCode;
}

WZResult &WZResult::operator=(const WZResult &a2) // OK
{
    this->m_dwErrorCode = a2.m_dwErrorCode;
    this->m_dwWindowErrorCode = a2.m_dwWindowErrorCode;
    StringCchCopy(this->m_szErrorMessage, std::size(this->m_szErrorMessage), a2.m_szErrorMessage);
    return *this;
}

void WZResult::SetResult(DWORD dwErrorCode, DWORD dwWindowErrorCode, const TCHAR *szFormat,
                         ...) // OK
{
    va_list va;

    va_start(va, szFormat);
    this->m_dwErrorCode = dwErrorCode;
    this->m_dwWindowErrorCode = dwWindowErrorCode;
    StringCchVPrintf(this->m_szErrorMessage, std::size(this->m_szErrorMessage), szFormat, va);
}

void WZResult::SetSuccessResult() // OK
{
    this->m_dwErrorCode = WZ_SUCCESS;
    this->m_dwWindowErrorCode = ERROR_SUCCESS;
    StringCchCopy(this->m_szErrorMessage, std::size(this->m_szErrorMessage), L"Success");
}

WZResult WZResult::BuildSuccessResult() // OK
{
    WZResult result;

    result.SetSuccessResult();

    return result;
}

WZResult WZResult::BuildResult(DWORD dwErrorCode, DWORD dwWindowErrorCode, const TCHAR *szFormat,
                               ...) // OK
{
    WZResult result;
    wchar_t Buffer[MAX_ERROR_MESSAGE];
    va_list args;

    va_start(args, szFormat);

    memset(Buffer, 0, sizeof(Buffer));
    StringCchVPrintf(Buffer, std::size(Buffer), szFormat, args);

    result.SetResult(dwErrorCode, dwWindowErrorCode, Buffer);

    return result;
}

#endif

namespace
{
constexpr std::int32_t kDefaultItemIndex = 0;
constexpr std::uint32_t kDefaultItemCost = 0;

bool IsValidItemIndex(std::int32_t index) noexcept
{
    return index >= 0;
}
} // namespace

GambleSystem::GambleSystem() = default;

void GambleSystem::Init()
{
    m_isGambleShop = false;
    m_buyItemPosition = 0;
    m_itemInfo.ItemIndex = kDefaultItemIndex;
    m_itemInfo.ItemCost = kDefaultItemCost;
}

void GambleSystem::SetBuyItemInfo(const std::int32_t index, const std::uint32_t cost)
{
    if (IsValidItemIndex(index))
    {
        m_itemInfo.ItemIndex = index;
        m_itemInfo.ItemCost = cost;
    }
    else
    {
        m_itemInfo.ItemIndex = kDefaultItemIndex;
        m_itemInfo.ItemCost = kDefaultItemCost;
    }
}

bool SessionGameDataUnit::CreatePersonalItemTable()
{
    if (sessionKeeper_.PersonalItemPriceStorage().initialized)
        return false;
    sessionKeeper_.PersonalItemPriceStorage().seller.clear();
    sessionKeeper_.PersonalItemPriceStorage().buyer.clear();
    sessionKeeper_.PersonalItemPriceStorage().initialized = true;
    return true;
}

void SessionGameDataUnit::ReleasePersonalItemTable()
{
    sessionKeeper_.PersonalItemPriceStorage().seller.clear();
    sessionKeeper_.PersonalItemPriceStorage().buyer.clear();
    sessionKeeper_.PersonalItemPriceStorage().initialized = false;
}

void SessionGameDataUnit::AddPersonalItemPrice(int index, int price, int type)
{
    if (!sessionKeeper_.PersonalItemPriceStorage().initialized)
        return;
    auto *table = type == 1   ? &sessionKeeper_.PersonalItemPriceStorage().seller
                  : type == 2 ? &sessionKeeper_.PersonalItemPriceStorage().buyer
                              : nullptr;
    if (table != nullptr)
        (*table)[index] = price;
}

void SessionGameDataUnit::RemovePersonalItemPrice(int index, int type)
{
    if (!sessionKeeper_.PersonalItemPriceStorage().initialized)
        return;
    auto *table = type == 1   ? &sessionKeeper_.PersonalItemPriceStorage().seller
                  : type == 2 ? &sessionKeeper_.PersonalItemPriceStorage().buyer
                              : nullptr;
    if (table != nullptr)
        table->erase(index);
}

void SessionGameDataUnit::RemoveAllPerosnalItemPrice(int type)
{
    if (!sessionKeeper_.PersonalItemPriceStorage().initialized)
        return;
    auto *table = type == 1   ? &sessionKeeper_.PersonalItemPriceStorage().seller
                  : type == 2 ? &sessionKeeper_.PersonalItemPriceStorage().buyer
                              : nullptr;
    if (table != nullptr)
        table->clear();
}

bool SessionGameDataUnit::GetPersonalItemPrice(int index, int &price, int type)
{
    if (!sessionKeeper_.PersonalItemPriceStorage().initialized)
        return false;
    auto *table = type == 1   ? &sessionKeeper_.PersonalItemPriceStorage().seller
                  : type == 2 ? &sessionKeeper_.PersonalItemPriceStorage().buyer
                              : nullptr;
    if (table == nullptr)
        return false;
    const auto found = table->find(index);
    if (found == table->end())
        return false;
    price = found->second;
    return true;
}
