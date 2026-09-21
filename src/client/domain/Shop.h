#pragma once
#include "support/CoreMath.h"
#include "session/SessionRuntime.h"
#include "app/ApplicationNetwork.h"
#include <cstdint>
#include <string>
#include <vector>
#include <time.h>
#include <map>
#include <cstddef>
#include <list>
#include <utility>
#ifdef _WIN32
#include <tchar.h>
#endif

#define WZ_SUCCESS 0x00000000
#define WZ_USER_BREAK 0x00000001
#define GU_FAILED 0xF0000000
#define GU_EMPTY_FILELIST (0x00000001 | GU_FAILED)
#define VS_FAILED 0x10000000
#define VS_EXCEPTION (0x00000001 | DL_FAILED)
#define VS_NO_INFO (0x00000002 | VS_FAILED)
#define VS_OPEN_VERSION_FILE (0x00000003 | VS_FAILED)
#define VS_READ_VERSION_FILE (0x00000004 | VS_FAILED)
#define VS_WRITE_VERSION_FILE (0x00000005 | VS_FAILED)
#define VS_INVALID_VERSION (0x00000006 | VS_FAILED)
#define DL_FAILED 0x20000000
#define DL_EXCEPTION (0x00000001 | DL_FAILED)
#define DL_NO_INFO (0x00000002 | DL_FAILED)
#define DL_BEGIN_THREAD_CONNECTION (0x00000003 | DL_FAILED)
#define DL_CONNECTION_TIMEOUT (0x00000004 | DL_FAILED)
#define DL_LOCALFILE_EXISTS (0x00000005 | DL_FAILED)
#define DL_CREATE_LOCALFILE (0x00000006 | DL_FAILED)
#define DL_DIFFERENT_FILE_LENGTH (0x00000007 | DL_FAILED)
#define DL_WRITE_LOCALFILE (0x00000008 | DL_FAILED)
#define DL_CREATE_SESSION (0x00000009 | DL_FAILED)
#define DL_CREATE_CONNECTION (0x0000000A | DL_FAILED)
#define DL_GET_FILE_LENGTH (0x0000000B | DL_FAILED)
#define DL_OPEN_REMOTEFILE (0x0000000C | DL_FAILED)
#define DL_READ_REMOTEFILE (0x0000000D | DL_FAILED)
#define DL_HTTP_QUERY_INFO (0x0000000E | DL_FAILED)
#define DL_HTTP_STATUS_NOT_OK (0x0000000F | DL_FAILED)
#define PT_FAILED 0x40000000
#define PT_EXCEPTION (0x00000001 | PT_FAILED)
#define PT_NO_INFO (0x00000002 | PT_FAILED)
#define PT_NO_DLL_INFO (0x00000003 | PT_FAILED)
#define PT_LOADLIBRARY (0x00000004 | PT_FAILED)
#define PT_GETPROCADDR (0x00000005 | PT_FAILED)
#define PT_UNPATCH_IN_ADAPTER (0x00000006 | PT_FAILED)
#define PT_CHECKFILES_IN_ADAPTER (0x00000007 | PT_FAILED)
#define ST_FAILED 0x80000000
#define ST_EXCEPTION (0x00000001 | ST_FAILED)
#define ST_XML_OPEN (0x00000002 | ST_FAILED)
#define ST_LOADLIBRARY_CRYPTDLL (0x00000003 | ST_FAILED)
#define ST_DECRYPT (0x00000004 | ST_FAILED)
#define ST_INVALID_XML (0x00000005 | ST_FAILED)
#define ST_BAD_PTR (0x00000006 | ST_FAILED)
#define ST_COMMON_INFO (0x00000007 | ST_FAILED)
#define ST_LAUNCHER_INFO (0x00000008 | ST_FAILED)
#define ST_GAME_INFO (0x00000009 | ST_FAILED)
#define ST_GAME_UI_INFO (0x0000000A | ST_FAILED)
#define ST_GAME_PATH (0x0000000B | ST_FAILED)
#define ST_SERVER_INFO (0x0000000C | ST_FAILED)
#define ST_VERSION_INFO (0x0000000D | ST_FAILED)
#define ST_PATCHER_INFO (0x0000000E | ST_FAILED)
#define MAX_ERROR_MESSAGE 1024
#define DL_DEFAULT_BUFFER_SIZE 4096

struct BuyItemInfo
{
    union {
        struct
        {
            std::int32_t itemIndex;
            std::uint32_t itemCost;
        };
        struct
        {
            std::int32_t ItemIndex;
            std::uint32_t ItemCost;
        };
    };

    constexpr BuyItemInfo() noexcept : itemIndex(0), itemCost(0)
    {
    }
};
using BuyItemInfoPtr = BuyItemInfo *;
using LPBUYITEMINFO = BuyItemInfoPtr; // Legacy alias; prefer BuyItemInfoPtr going forward.

class GambleSystem final
{
  public:
    GambleSystem();

    GambleSystem(const GambleSystem &) = delete;
    GambleSystem &operator=(const GambleSystem &) = delete;
    GambleSystem(GambleSystem &&) = delete;
    GambleSystem &operator=(GambleSystem &&) = delete;

    ~GambleSystem() = default;

    void Init();

    void SetGambleShop(bool isGambleShop = true)
    {
        m_isGambleShop = isGambleShop;
    }
    bool IsGambleShop() const
    {
        return m_isGambleShop;
    }

    void SetBuyItemInfo(std::int32_t index, std::uint32_t cost);
    const BuyItemInfo &GetBuyItemInfoConst() const
    {
        return m_itemInfo;
    }

    void SetBuyItemPosition(std::uint8_t position)
    {
        m_buyItemPosition = position;
    }
    std::uint8_t GetBuyItemPosition() const
    {
        return m_buyItemPosition;
    }

  private:
    bool m_isGambleShop{false};
    std::uint8_t m_buyItemPosition{0};
    BuyItemInfo m_itemInfo{};
};

// libcurl-backed shop file downloader (issue #462).
// The in-game shop's catalog/script downloader is a WinINet + urlmon stack on
// Windows (FileDownloader/FTPConnecter/HTTPConnecter). Those have no portable
// equivalent, so off Windows this class downloads a single remote file (FTP or
// HTTP) to a local path with libcurl instead. It is compiled only on
// non-Windows; the WinINet path stays in use on Windows unchanged.

class WZResult
{
  public:
    // Constructor, Destructor
    WZResult();
    ~WZResult();

    // public Function

    WZResult &operator=(const WZResult &result);
    BOOL IsSuccess();
    TCHAR *GetErrorMessage();
    DWORD GetErrorCode();
    DWORD GetWindowErrorCode();

    void SetSuccessResult();
    void SetResult(DWORD dwErrorCode, DWORD dwWindowErrorCode, const TCHAR *szFormat, ...);

    static WZResult BuildSuccessResult();
    static WZResult BuildResult(DWORD dwErrorCode, DWORD dwWindowErrorCode, const TCHAR *szFormat,
                                ...);

  private:
    // Member Object
    DWORD m_dwErrorCode;
    DWORD m_dwWindowErrorCode;
    TCHAR m_szErrorMessage[MAX_ERROR_MESSAGE];
};

class CurlFileDownloader
{
  public:
    // Downloads url to localPath, creating parent directories as needed.
    // username/password are used for FTP authentication (ignored when empty).
    // passiveFtp selects passive vs. active FTP transfer mode. pBreak, when not
    // null, is polled during the transfer and aborts it once it becomes
    // non-zero. Returns a WZResult using the same DL_/WZ_ codes as the WinINet
    // downloader so callers handle both paths identically.
    static WZResult DownloadFile(const std::wstring &url, const std::wstring &localPath,
                                 const std::wstring &username, const std::wstring &password,
                                 bool passiveFtp, const volatile int *pBreak);
};

typedef enum _DownloaderType
{
    FTP,
    HTTP,
} DownloaderType;

class DownloadFileInfo
{
  public:
    // Constructor, Destructor

    DownloadFileInfo();
    ~DownloadFileInfo();

    // Get Function
    TCHAR *GetFileName();
    TCHAR *GetLocalFilePath();
    TCHAR *GetRemoteFilePath();
    TCHAR *GetTargetDirPath();
    ULONGLONG GetFileLength();
    // Set Function
    void SetFilePath(TCHAR *szFileName, TCHAR *szLocalFilePath, TCHAR *szRemoteFilePath,
                     TCHAR *szTargerDirPath);
    void SetFileLength(ULONGLONG uFileLength);

  private:
    // Member Object

    TCHAR m_szFileName[MAX_PATH];
    TCHAR m_szLocalFilePath[MAX_PATH];
    TCHAR m_szRemoteFilePath[INTERNET_MAX_URL_LENGTH];
    TCHAR m_szTargerDirPath[MAX_PATH];
    ULONGLONG m_uFileLength;
};

class DownloadServerInfo
{
  public:
    // Constructor, Destructor

    DownloadServerInfo();
    ~DownloadServerInfo();

    // Get Function

    TCHAR *GetServerURL();
    TCHAR *GetUserID();
    TCHAR *GetPassword();
    INTERNET_PORT GetPort();
    DownloaderType GetDownloaderType();
    DWORD GetReadBufferSize();
    DWORD GetConnectTimeout();
    BOOL IsOverWrite();
    BOOL IsPassive();

    // Set Function

    void SetServerInfo(TCHAR *szServerURL, INTERNET_PORT nPort, TCHAR *szUserID, TCHAR *szPassword);
    void SetDownloaderType(DownloaderType dwDownloaderType);
    void SetReadBufferSize(DWORD dwReadBufferSize);
    void SetOverWrite(BOOL bOverWrite);
    void SetPassiveMode(BOOL bPassive);
    void SetConnectTimeout(DWORD dwConnectTimeout);

  private:
    // Member Object

    TCHAR m_szServerURL[INTERNET_MAX_URL_LENGTH];
    TCHAR m_szUserID[INTERNET_MAX_USER_NAME_LENGTH];
    TCHAR m_szPassword[INTERNET_MAX_PASSWORD_LENGTH];
    INTERNET_PORT m_nPort;
    DownloaderType m_DownloaderType;
    DWORD m_dwReadBufferSize;
    BOOL m_bOverWrite;
    BOOL m_bPassive;
    DWORD m_dwConnectTimeout;
};

class IConnecter
{
  public:
    // Constructor, Destructor

    IConnecter(DownloadServerInfo *pServerInfo, DownloadFileInfo *pFileInfo)
        : m_pServerInfo(pServerInfo), m_pFileInfo(pFileInfo) {};
    ~IConnecter() {};

    // abstract Function

    virtual WZResult CreateSession(HINTERNET &hSession) = 0;
    virtual WZResult CreateConnection(HINTERNET &hSession, HINTERNET &hConnection) = 0;
    virtual WZResult OpenRemoteFile(HINTERNET &hConnection, HINTERNET &hRemoteFile,
                                    ULONGLONG &nFileLength) = 0;
    virtual WZResult ReadRemoteFile(HINTERNET &hRemoteFile, BYTE *byReadBuffer,
                                    DWORD *dwBytesRead) = 0;

  protected:
    // Member Object

    WZResult m_Result;
    DownloadServerInfo *m_pServerInfo;
    DownloadFileInfo *m_pFileInfo;
};

class FTPConnecter : public IConnecter
{
  public:
    // Constructor, Destructor

    FTPConnecter(DownloadServerInfo *pServerInfo, DownloadFileInfo *pFileInfo);
    ~FTPConnecter();

    // abstract Function

    virtual WZResult CreateSession(HINTERNET &hSession);
    virtual WZResult CreateConnection(HINTERNET &hSession, HINTERNET &hConnection);
    virtual WZResult OpenRemoteFile(HINTERNET &hConnection, HINTERNET &hRemoteFile,
                                    ULONGLONG &nFileLength);
    virtual WZResult ReadRemoteFile(HINTERNET &hRemoteFile, BYTE *byReadBuffer, DWORD *dwBytesRead);
};

class HTTPConnecter : public IConnecter
{
  public:
    // Constructor, Destructor

    HTTPConnecter(DownloadServerInfo *pServerInfo, DownloadFileInfo *pFileInfo);
    ~HTTPConnecter();

    // abstract Function

    virtual WZResult CreateSession(HINTERNET &hSession);
    virtual WZResult CreateConnection(HINTERNET &hSession, HINTERNET &hConnection);
    virtual WZResult OpenRemoteFile(HINTERNET &hConnection, HINTERNET &hRemoteFile,
                                    ULONGLONG &nFileLength);
    virtual WZResult ReadRemoteFile(HINTERNET &hRemoteFile, BYTE *byReadBuffer, DWORD *dwBytesRead);
};

#pragma warning(disable : 4995)

class IDownloaderStateEvent
{
  public:
    // Constructor, Destructor

    IDownloaderStateEvent() {};
    virtual ~IDownloaderStateEvent() {};

    // abstract Function

    virtual void OnStartedDownloadFile(TCHAR *szFileName, ULONGLONG uFileLength) = 0;
    virtual void OnProgressDownloadFile(TCHAR *szFileName, ULONGLONG uDownloadFileLength) = 0;
    virtual void OnCompletedDownloadFile(TCHAR *szFileName, WZResult wzResult) = 0;
};

#pragma comment(lib, "Wininet.lib")

#if !defined(AFX_INGAMESHOPSYSTEM_H__2DF68839_DA28_44BC_B662_213BB22839CB__INCLUDED_)
#define AFX_INGAMESHOPSYSTEM_H__2DF68839_DA28_44BC_B662_213BB22839CB__INCLUDED_

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

#define STRSAFE_NO_DEPRECATE

#define SHOPLIST_SCRIPT_COUNT 3

#define SHOPLIST_SCRIPT_CATEGORY L"IBSCategory.txt"
#define SHOPLIST_SCRIPT_PACKAGE L"IBSPackage.txt"
#define SHOPLIST_SCRIPT_PRODUCT L"IBSProduct.txt"
#define BANNER_SCRIPT_FILENAME L"IBSBanner.txt"

#define SHOPLIST_LENGTH_CATEGORYNAME 128

#define SHOPLIST_LENGTH_PACKAGENAME 512
#define SHOPLIST_LENGTH_PACKAGEDESC 2048
#define SHOPLIST_LENGTH_PACKAGECAUTION 1024
#define SHOPLIST_LENGTH_PACKAGECASHNAME 256
#define SHOPLIST_LENGTH_PACKAGEPRICEUNIT 64

#define SHOPLIST_LENGTH_PRODUCTNAME 128
#define SHOPLIST_LENGTH_PRODUCTPROPERTYNAME 128
#define SHOPLIST_LENGTH_PRODUCTVALUE 512
#define SHOPLIST_LENGTH_PRODUCTUNITNAME 64
#define SHOPLIST_LENGTH_INGAMEPACKAGEID 20

#define BANNER_LENGTH_NAME 50

#define ERROR_TIMEOUT_BREAK 0x01
#define ERROR_FILE_SIZE_ZERO 0x02
#define ERROR_CATEGORY_OPEN_FAIL 0x03
#define ERROR_PACKAGE_OPEN_FAIL 0x04
#define ERROR_PRODUCT_OPEN_FAIL 0x05
#define ERROR_BANNER_OPEN_FAIL 0x06
#define ERROR_LOAD_SCRIPT 0x07
#define ERROR_THREAD 0x08

class FileDownloader
{
  public:
    // Constructor, Destructor

    FileDownloader(IDownloaderStateEvent *pStateEvent, DownloadServerInfo *pServerInfo,
                   DownloadFileInfo *pFileInfo);
    ~FileDownloader();

    // public Function

    void Break();
    WZResult DownloadFile();

  private:
    // private Function

    BOOL CanBeContinue();
    void Release();

    IConnecter *CreateConnecter();
    WZResult CreateConnection();
    static unsigned int __stdcall RunConnectThread(LPVOID pParam);
    WZResult Connection();

    WZResult TransferRemoteFile();

    WZResult CreateLocalFile();
    WZResult ReadRemoteFile(BYTE *byReadBuffer, DWORD *dwBytesRead);
    WZResult WriteLocalFile(BYTE *byReadBuffer, DWORD dwBytesRead);

    void SendStartedDownloadFileEvent(ULONGLONG nFileLength);
    void SendCompletedDownloadFileEvent(WZResult wzResult);
    void SendProgressDownloadFileEvent(ULONGLONG nTotalBytesRead);

    // Member Object

    volatile BOOL m_bBreak;
    WZResult m_Result;

    IDownloaderStateEvent *m_pStateEvent;
    DownloadServerInfo *m_pServerInfo;
    DownloadFileInfo *m_pFileInfo;
    IConnecter *m_pConnecter;

    HINTERNET m_hSession;
    HINTERNET m_hConnection;
    HINTERNET m_hRemoteFile;
    HANDLE m_hLocalFile;
    ULONGLONG m_nFileLength;
};

#if !defined(INVALID_FILE_ATTRIBUTES)
#define INVALID_FILE_ATTRIBUTES ((DWORD) - 1)
#endif

enum FTP_SERVICE_MODE
{
    FTP_MODE_ACTIVE,
    FTP_MODE_PASSIVE
};
enum FILE_ENCODE
{
    FE_ANSI,
    FE_UTF8,
    FE_UNICODE
};

class CListVersionInfo
{
  public:
    unsigned short Zone;
    unsigned short year;
    unsigned short yearId;
};

class CListManagerInfo
{
  public:
    DownloaderType m_DownloaderType;
    std::wstring m_strServerIP;
    unsigned short m_nPortNum;
    std::wstring m_strUserID;
    std::wstring m_strPWD;
    std::wstring m_strRemotePath;
    FTP_SERVICE_MODE m_ftpMode;
    std::wstring m_strLocalPath;
    DWORD m_dwDownloadMaxTime;

    CListVersionInfo m_Version;
};

class CFTPFileDownLoader
{
  public:
    CFTPFileDownLoader();
    virtual ~CFTPFileDownLoader();

  public:
    WZResult DownLoadFiles(DownloaderType type, std::wstring strServerIP, unsigned short PortNum,
                           std::wstring strUserName, std::wstring strPWD,
                           std::wstring strRemotepath, std::wstring strlocalpath, bool bPassiveMode,
                           CListVersionInfo Version, std::vector<std::wstring> vScriptFiles);

    void Break();

    FileDownloader *GetFileDownloader()
    {
        return m_pFileDownloader;
    }

  private:
    BOOL CreateFolder(std::wstring strFilePath);
    BOOL m_Break;
    FileDownloader *m_pFileDownloader;
};

class CmuConsoleDebug;

class CListManager
{
  public:
    CListManager();
    virtual ~CListManager();

    void SetListManagerInfo(DownloaderType type, const wchar_t *ServerIP, const wchar_t *UserID,
                            const wchar_t *Pwd, const wchar_t *RemotePath, const wchar_t *LocalPath,
                            CListVersionInfo Version, DWORD dwDownloadMaxTime = 0);

    void SetListManagerInfo(DownloaderType type, const wchar_t *ServerIP, unsigned short PortNum,
                            const wchar_t *UserID, const wchar_t *Pwd, const wchar_t *RemotePath,
                            const wchar_t *LocalPath, FTP_SERVICE_MODE ftpMode,
                            CListVersionInfo Version, DWORD dwDownloadMaxTime = 0);

    WZResult LoadScriptList(bool bDonwLoad);
    WZResult LoadScriptList(bool bDonwLoad, CmuConsoleDebug &consoleDebug);

  protected:
    bool IsScriptFileExist();
    std::wstring GetScriptPath();
    void DeleteScriptFiles();

    WZResult FileDownLoad();
    WZResult FileDownLoadImpl();
    static unsigned int __stdcall RunFileDownLoadThread(LPVOID pParam);

    virtual WZResult LoadScript(bool bDonwLoad) = 0;
    virtual WZResult LoadScript(bool bDonwLoad, CmuConsoleDebug &consoleDebug);

    CListManagerInfo m_ListManagerInfo;
    std::vector<std::wstring> m_vScriptFiles;
    WZResult m_Result;
    CFTPFileDownLoader *m_pFTPDownLoader;
};
/* Shop list aggregate and its category, package, and product lists. */

class CShopPackage
{
  public:
    CShopPackage();
    virtual ~CShopPackage();

    bool SetPackage(std::wstring strdata);
    void SetLeftCount(int nCount);

    int GetProductCount();
    void SetProductSeqFirst();
    bool GetProductSeqFirst(int &ProductSeq);
    bool GetProductSeqNext(int &ProductSeq);

    int GetPriceCount();
    void SetPriceSeqFirst();
    bool GetPriceSeqFirst(int &PriceSeq);
    bool GetPriceSeqNext(int &PriceSeq);

  public:
    int ProductDisplaySeq;
    int ViewOrder;
    int PackageProductSeq;
    wchar_t PackageProductName[SHOPLIST_LENGTH_PACKAGENAME];
    int PackageProductType;
    int Price;
    wchar_t Description[SHOPLIST_LENGTH_PACKAGEDESC];
    wchar_t Caution[SHOPLIST_LENGTH_PACKAGECAUTION];
    int SalesFlag;
    int GiftFlag;
    tm StartDate;
    tm EndDate;
    int CapsuleFlag;
    int CapsuleCount;
    wchar_t ProductCashName[SHOPLIST_LENGTH_PACKAGECASHNAME];
    wchar_t PricUnitName[SHOPLIST_LENGTH_PACKAGEPRICEUNIT];
    int DeleteFlag;
    int EventFlag;
    int ProductAmount;
    wchar_t InGamePackageID[SHOPLIST_LENGTH_INGAMEPACKAGEID];
    int ProductCashSeq;
    int PriceCount;
    bool DeductMileageFlag;
    int CashType;
    int CashTypeFlag;

    int LeftCount;

  private:
    void SetProductSeqList(std::wstring strdata);
    void SetPriceSeqList(std::wstring strdata);

    std::vector<int> ProductSeqList;
    std::vector<int>::iterator ProductSeqIter;

    std::vector<int> PriceSeqList;
    std::vector<int>::iterator PriceSeqIter;
};

class CShopProduct
{
  public:
    CShopProduct();
    virtual ~CShopProduct();

    bool SetProduct(std::wstring strdata);

  public:
    int ProductSeq;
    wchar_t ProductName[SHOPLIST_LENGTH_PRODUCTNAME];
    wchar_t PropertyName[SHOPLIST_LENGTH_PRODUCTPROPERTYNAME];
    wchar_t Value[SHOPLIST_LENGTH_PRODUCTVALUE];
    wchar_t UnitName[SHOPLIST_LENGTH_PRODUCTUNITNAME];
    int Price;
    int PriceSeq;
    int PropertyType;
    int MustFlag;
    int vOrder;
    int DeleteFlag;
    int StorageGroup;
    int ShareFlag;
    wchar_t InGamePackageID[SHOPLIST_LENGTH_INGAMEPACKAGEID];
    int PropertySeq;
    int ProductType;
    int UnitType;
};

class CShopCategory
{
  public:
    CShopCategory();
    virtual ~CShopCategory();

    bool SetCategory(std::wstring strdata);

    void SetCategoryFirst();
    bool GetCategoryNext(int &CategorySeq);

    void SetPackagSeqFirst();
    bool GetPackagSeqNext(int &PackagSeq);

    void AddPackageSeq(int PackageSeq);
    void ClearPackageSeq();

  public:
    int ProductDisplaySeq;
    wchar_t CategroyName[SHOPLIST_LENGTH_CATEGORYNAME];
    int EventFlag;
    int OpenFlag;
    int ParentProductDisplaySeq;
    int DisplayOrder;
    int Root;

    std::vector<int> CategoryList;
    std::vector<int>::iterator Categoryiter;

    std::vector<int> PackageList;
    std::vector<int>::iterator Packageiter;
};

class CShopCategoryList
{
  public:
    CShopCategoryList(void);
    ~CShopCategoryList(void);

    void Clear();

    int GetSize();
    virtual void Append(CShopCategory category);

    void SetFirst();
    bool GetNext(CShopCategory &category);

    bool GetValueByKey(int nKey, CShopCategory &category);
    bool GetValueByIndex(int nIndex, CShopCategory &category);

    bool InsertPackage(int Category, int Package);
    bool RefreshPackageSeq(int Category, int PackageSeqs[], int PackageCount);

  protected:
    std::map<int, CShopCategory> m_Categroys;
    std::map<int, CShopCategory>::iterator m_Categoryiter;
    std::vector<int> m_CategoryIndex;
};

class CShopPackageList
{
  public:
    CShopPackageList(void);
    ~CShopPackageList(void);

    int GetSize();
    void Clear();

    virtual void Append(CShopPackage package);

    void SetFirst();
    bool GetNext(CShopPackage &package);

    bool GetValueByKey(int nKey, CShopPackage &package);
    bool GetValueByIndex(int nIndex, CShopPackage &package);

    bool SetPacketLeftCount(int PackageSeq, int nCount);

  protected:
    std::map<int, CShopPackage> m_Packages;
    std::map<int, CShopPackage>::iterator m_iter;
    std::vector<int> m_PackageIndex;
};

class CShopProductList
{
  public:
    CShopProductList(void);
    ~CShopProductList(void);

    void Clear();
    int GetSize();

    virtual void Append(CShopProduct product);

    void SetFirst();
    bool GetNext(CShopProduct &product);

    void SetProductSeqFirst(int ProductSeq);
    bool GetProductSeqNext(CShopProduct &product);

    void SetPriceSeqFirst(int ProductSeq, int PriceSeq);
    bool GetPriceSeqNext(CShopProduct &product);

  protected:
    int PriceSeqKey;
    std::multimap<int, CShopProduct> m_Products;
    std::multimap<int, CShopProduct>::iterator m_ProductIter;
    std::multimap<int, CShopProduct>::iterator m_ProductSeqIter;
    std::multimap<int, CShopProduct>::iterator m_PriceSeqIter;
    std::pair<std::multimap<int, CShopProduct>::iterator,
              std::multimap<int, CShopProduct>::iterator>
        m_ProductRange;
    std::pair<std::multimap<int, CShopProduct>::iterator,
              std::multimap<int, CShopProduct>::iterator>
        m_PriceRange;
};

class CmuConsoleDebug;

class CShopList : protected SessionLegacyCalls
{
  public:
    explicit CShopList(SessionKeeper &keeper);
    virtual ~CShopList();

    WZResult LoadCategroy(const wchar_t *szFilePath);
    WZResult LoadPackage(const wchar_t *szFilePath);
    WZResult LoadProduct(const wchar_t *szFilePath);

    CShopCategoryList *GetCategoryListPtr()
    {
        return m_CategoryListPtr;
    }; // 카테고리 목록 가져온다.
    CShopPackageList *GetPackageListPtr()
    {
        return m_PackageListPtr;
    }; // 패키지 목록 가져온다.
    CShopProductList *GetProductListPtr()
    {
        return m_ProductListPtr;
    }; // 상품(속성) 목록 가져온다.

    void SetCategoryListPtr(CShopCategoryList *CategoryListPtr);
    void SetPackageListPtr(CShopPackageList *PackagePtr);
    void SetProductListPtr(CShopProductList *ProductListPtr);

  private:
    CmuConsoleDebug &g_ConsoleDebug;
    CShopCategoryList *m_CategoryListPtr;
    CShopPackageList *m_PackageListPtr;
    CShopProductList *m_ProductListPtr;

    FILE_ENCODE IsFileEncodingUtf8(const wchar_t *szFilePath);
    std::wstring GetDecodedString(const char *buffer, FILE_ENCODE encode);
};

class CShopListManager : public CListManager, protected SessionLegacyCalls
{
  public:
    explicit CShopListManager(SessionKeeper &keeper);
    virtual ~CShopListManager();

    CShopList *GetListPtr()
    {
        return m_ShopList;
    };

  private:
    CShopList *m_ShopList;

    WZResult LoadScript(bool bDonwLoad) override;
};

class CBannerInfo
{
  public:
    CBannerInfo();
    virtual ~CBannerInfo();

    bool SetBanner(std::wstring strdata, std::wstring strDirPath, bool bDonwLoad);

  public:
    int BannerSeq;
    wchar_t BannerName[BANNER_LENGTH_NAME];
    wchar_t BannerImageURL[INTERNET_MAX_URL_LENGTH];
    int BannerOrder;
    int BannerDirection;
    tm BannerStartDate;
    tm BannerEndDate;
    wchar_t BannerLinkURL[INTERNET_MAX_URL_LENGTH];

    wchar_t BannerImagePath[MAX_PATH];
};

class CBannerInfoList
{
  public:
    CBannerInfoList(void);
    ~CBannerInfoList(void);

    WZResult LoadBanner(std::wstring strDirPath, std::wstring strScriptFileName, bool bDonwLoad);

    void Clear();
    int GetSize();

    virtual void Append(CBannerInfo banner);

    void SetFirst();
    bool GetNext(CBannerInfo &banner);

  protected:
    std::multimap<int, CBannerInfo> m_BannerInfos;
    std::multimap<int, CBannerInfo>::iterator m_BannerInfoIter;
};

class CBannerListManager : public CListManager
{
  public:
    CBannerListManager();
    virtual ~CBannerListManager();

    CBannerInfoList *GetListPtr()
    {
        return m_BannerInfoList;
    };

  private:
    CBannerInfoList *m_BannerInfoList;

    WZResult LoadScript(bool bDonwLoad);
};

#define INGAMESHOP_ERROR_ZERO_SIZE (-1)
#define INGAMESHOP_ERROR_INVALID_INDEX (-2)

typedef std::list<CShopPackage> type_listPackage;
typedef std::map<int, int> type_mapZoneSeq;
typedef std::list<std::wstring> type_listName;

class CmuConsoleDebug;

class CInGameShopSystem : protected SessionLegacyCalls
{
  public:
    enum IGS_PACKAGE_GOODS_TYPE
    {
        IGS_GOODS_TYPE_FIXEDAMOUNT = 135,
        IGS_GOODS_TYPE_FIXEDQUANTITY = 136,
        IGS_GOODS_TYPE_PREMIUM = 137,
        IGS_GOODS_TYPE_CONSUMPTION = 138,
        IGS_GOODS_TYPE_ETERNITY = 139,
        IGS_GOODS_TYPE_PERIOD = 140,
        IGS_GOODS_TYPE_PREMIUM_ITEM = 406,
        IGS_GOODS_TYPE_GOBLIN_POINT = 515,
    };

    enum IGS_PACKAGE_ATTRIBUTE_TYPE
    {
        IGS_PACKAGE_ATT_TYPE_NONE = 0,
        IGS_PACKAGE_ATT_TYPE_NAME,
        IGS_PACKAGE_ATT_TYPE_DESCRIPTION,
        IGS_PACKAGE_ATT_TYPE_PRICE,
        IGS_PACKAGE_ATT_TYPE_ITEMCODE,
    };

    enum IGS_PRODUCT_ATTRIBUTE_TYPE
    {
        IGS_PRODUCT_ATT_TYPE_NONE = 0,
        IGS_PRODUCT_ATT_TYPE_USE_LIMIT_PERIOD,
        IGS_PRODUCT_ATT_TYPE_AVALIABLE_PERIOD,
        IGS_PRODUCT_ATT_TYPE_NUM,
        IGS_PRODUCT_ATT_TYPE_PRICE,
        IGS_PRODUCT_ATT_TYPE_ITEMCODE,
        IGS_PRODUCT_ATT_TYPE_ITEMNAME,
        IGS_PRODUCT_ATT_TYPE_PRICE_SEQUENCE,
    };

    enum IGS_ETC
    {
        IGS_LIMIT_REQUEST_EVENT_PACKAGE = 20,
    };

  public:
    explicit CInGameShopSystem(SessionKeeper &keeper);
    ~CInGameShopSystem();

    void Initalize();
    // 	bool Update();
    // 	bool Render();
    void Release();

    void SetScriptVersion(int iSalesZone, int iYear, int iYearId);
    void SetBannerVersion(int iSalesZone, int iYear, int iYearId);
    bool ScriptDownload();
    bool BannerDownload();

  public:
    bool SelectZone(int iIndex);
    bool SelectCategory(int iIndex);

    void BeginPage();
    void NextPage();
    void PrePage();
    int GetTotalPages();
    int GetSelectPage();

    int GetSizeZones();
    int GetSizeCategoriesAsSelectedZone();
    int GetSizePackageAsSelectedCategory();
    int GetSizePackageAsDisplayPackage();

    WORD GetPackageItemCode(int iIndex);

    type_listName &GetZoneName();
    type_listName &GetCategoryName();

    void SetTotalCash(double dTotalCash);
    void SetTotalPoint(double dTotalPoint);
    void SetTotalMileage(double dTotalMileage);
    void SetCashCreditCard(double dCashCreditCard); // Global Credit Cash
    void SetCashPrepaid(double dCashPrepaid);       // Global Prepaid Cash
    double GetTotalCash();
    double GetTotalPoint();
    double GetTotalMileage();
    double GetCashCreditCard(); // Global Credit Cash
    double GetCashPrepaid();    // Global Prepaid Cash

    CShopPackage *GetDisplayPackage(int iIndex);

    void SetIsRequestShopOpenning(bool bIsRequestShopOpenning);
    bool GetIsRequestShopOpenning();

    bool GetPackageInfo(int iPackageSeq, int iPackageAttrType, OUT int &iValue,
                        OUT wchar_t *pszText);

    bool GetProductInfoFromPriceSeq(int iProductSeq, int iPriceSeq, int iAttrType, OUT int &iValue,
                                    OUT wchar_t *pszUnitName);
    bool GetProductInfoFromProductSeq(int iProductSeq, int iAttrType, OUT int &iValue,
                                      OUT wchar_t *pszUnitName);

    void SetNormalPackage();
    void InitEventPackage(int iTotalEventPackage);
    void InsertEventPackage(int *pPackageSeq);

    bool IsShopOpen();
    bool IsRequestEventPackge();
    void SetRequestEventPackge();

    bool IsBanner();
    wchar_t *GetBannerFileName();
    wchar_t *GetBannerURL();

    CListVersionInfo GetScriptVer();
    CListVersionInfo GetBannerVer();
#ifdef KJH_MOD_SHOP_SCRIPT_DOWNLOAD
    CListVersionInfo GetCurrentScriptVer();
    CListVersionInfo GetCurrentBannerVer();
    bool IsScriptDownload();
    bool IsBannerDownload();

    void ShopOpenLock();
    void ShopOpenUnLock();
#endif // KJH_MOD_SHOP_SCRIPT_DOWNLOAD

  protected:
    void InitZoneInfo();
    void InitPackagePerPage(int iPageIndex);

    int GetZoneSeqIndexByIndex(int iIndex);
    int GetCategorySeqIndexByIndex(int iIndex);

    void SetCategoryName();

#ifndef KJH_MOD_SHOP_SCRIPT_DOWNLOAD
    void ShopOpenLock();
    void ShopOpenUnLock();
#endif // KJH_MOD_SHOP_SCRIPT_DOWNLOAD

    bool GetProductInfo(CShopProduct *pProduct, int iAttrType, OUT int &iValue,
                        OUT wchar_t *pszUnitName);

  protected:
    CmuConsoleDebug &g_ConsoleDebug;
    CShopListManager m_ShopManager;
    CBannerListManager m_BannerManager;

    CListVersionInfo m_ScriptVerInfo;
    CListVersionInfo m_BannerVerInfo;

#ifdef KJH_MOD_SHOP_SCRIPT_DOWNLOAD
    CListVersionInfo m_CurrentScriptVerInfo;
    CListVersionInfo m_CurrentBannerVerInfo;
#endif // KJH_MOD_SHOP_SCRIPT_DOWNLOAD

    wchar_t m_szScriptIPAddress[20];
    wchar_t m_szBannerIPAddress[20];
    wchar_t m_szScriptRemotePath[MAX_TEXT_LENGTH];
    wchar_t m_szScriptLocalPath[MAX_TEXT_LENGTH];
    wchar_t m_szBannerRemotePath[MAX_TEXT_LENGTH];
    wchar_t m_szBannerLocalPath[MAX_TEXT_LENGTH];

    CShopCategory m_SelectedZone;
    CShopCategory m_SelectedCategory;
    CBannerInfo m_BannerInfo;

    int m_iSelectedPage;

    type_mapZoneSeq m_mapZoneSeqIndex;
    type_listPackage m_listDisplayPackage;
    type_listPackage m_listNormalPackage;
    type_listPackage m_listEventPackage;
    type_listPackage *m_plistSelectPackage;
    type_listName m_listZoneName;
    type_listName m_listCategoryName;

    CShopCategoryList *m_pCategoryList;
    CShopPackageList *m_pPackageList;
    CShopProductList *m_pProductList;

    CBannerInfoList *m_pBannerList;

    double m_dTotalCash;
    double m_dTotalPoint;
    double m_dTotalMileage;
    double m_dCashCreditCard;
    double m_dCashPrepaid;

    bool m_bIsRequestEventPackage;

    bool m_bIsRequestShopOpenning;

    bool m_bSelectEventCategory;
    bool m_bAbleRequestEventPackage;
    int m_iCntSelectEventZone;

    int m_iEventPackageCnt;
    int m_iTotalEventPackage;
    int m_iCurrentEventPackage;

    bool m_bIsShopOpenLock;
    bool m_bIsBanner;
    bool m_bFirstScriptDownloaded;
    bool m_bFirstBannerDownloaded;
};

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
#endif // !defined(AFX_INGAMESHOPSYSTEM_H__2DF68839_DA28_44BC_B662_213BB22839CB__INCLUDED_)

class CInGameShopSystem;
class SessionKeeper;

CInGameShopSystem *CreateSessionInGameShopSystem(SessionKeeper &keeper);
void DestroySessionInGameShopSystem(CInGameShopSystem *system) noexcept;

class CStringMethod
{
  public:
    CStringMethod();
    virtual ~CStringMethod();

    static void ConvertStringToDateTime(tm &datetime, std::wstring strdata);
};

class CStringToken
{
  public:
    CStringToken();
    virtual ~CStringToken();
    CStringToken(const std::wstring &dataLine, const std::wstring &delim);

    size_t countTokens();
    bool hasMoreTokens();
    std::wstring nextToken();

  private:
    std::wstring data;
    std::wstring delimiter;
    std::vector<std::wstring> tokens;
    std::vector<std::wstring>::iterator index;

    void split();
    void IsNullString(std::wstring::size_type pos);
};

#pragma warning(disable : 4995)

class Path
{
  public:
    static TCHAR *GetCurrentFullPath(TCHAR *szPath);
    static TCHAR *GetCurrentDirectory(TCHAR *szPath);
    static TCHAR *GetCurrentFileName(TCHAR *szPath);

    static TCHAR *SetDirString(TCHAR *szPath);
    static TCHAR *ClearDirString(TCHAR *szPath);

    static TCHAR *GetDirectory(TCHAR *szPath);
    static TCHAR *GetFileName(TCHAR *szPath);

    static TCHAR *ChangeSlashToBackSlash(TCHAR *szPath);
    static TCHAR *ChangeBackSlashToSlash(TCHAR *szPath);

    static BOOL ReadFileLastLine(TCHAR *szFile, TCHAR *szLastLine);
    static BOOL WriteNewFile(TCHAR *szFile, TCHAR *szText, INT nTextSize);
    static BOOL CreateDirectorys(TCHAR *szFilePath, BOOL bIsFile);
};
