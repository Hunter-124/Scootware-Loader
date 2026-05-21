typedef unsigned char   undefined;

typedef unsigned char    bool;
typedef unsigned char    byte;
typedef unsigned int    dword;
typedef unsigned long long    GUID;
typedef pointer32 ImageBaseOffset32;

typedef long long    longlong;
typedef unsigned long long    qword;
typedef unsigned char    uchar;
typedef unsigned int    uint;
typedef unsigned long    ulong;
typedef unsigned long long    ulonglong;
typedef unsigned char    undefined1;
typedef unsigned short    undefined2;
typedef unsigned int    undefined4;
typedef unsigned long long    undefined8;
typedef unsigned short    ushort;
typedef unsigned short    wchar16;
typedef short    wchar_t;
typedef unsigned short    word;
#define unkbyte9   unsigned long long
#define unkbyte10   unsigned long long
#define unkbyte11   unsigned long long
#define unkbyte12   unsigned long long
#define unkbyte13   unsigned long long
#define unkbyte14   unsigned long long
#define unkbyte15   unsigned long long
#define unkbyte16   unsigned long long

#define unkuint9   unsigned long long
#define unkuint10   unsigned long long
#define unkuint11   unsigned long long
#define unkuint12   unsigned long long
#define unkuint13   unsigned long long
#define unkuint14   unsigned long long
#define unkuint15   unsigned long long
#define unkuint16   unsigned long long

#define unkint9   long long
#define unkint10   long long
#define unkint11   long long
#define unkint12   long long
#define unkint13   long long
#define unkint14   long long
#define unkint15   long long
#define unkint16   long long

#define unkfloat1   float
#define unkfloat2   float
#define unkfloat3   float
#define unkfloat5   double
#define unkfloat6   double
#define unkfloat7   double
#define unkfloat9   long double
#define unkfloat11   long double
#define unkfloat12   long double
#define unkfloat13   long double
#define unkfloat14   long double
#define unkfloat15   long double
#define unkfloat16   long double

#define BADSPACEBASE   void
#define code   void

typedef struct _s__RTTIBaseClassDescriptor _s__RTTIBaseClassDescriptor, *P_s__RTTIBaseClassDescriptor;

typedef struct _s__RTTIBaseClassDescriptor RTTIBaseClassDescriptor;

typedef RTTIBaseClassDescriptor *RTTIBaseClassDescriptor *32 __((image-base-relative));

typedef RTTIBaseClassDescriptor *32 __((image-base-relative)) *RTTIBaseClassDescriptor *32 __((image-base-relative)) *32 __((image-base-relative));

typedef struct PMD PMD, *PPMD;

struct PMD {
    int mdisp;
    int pdisp;
    int vdisp;
};

struct _s__RTTIBaseClassDescriptor {
    ImageBaseOffset32 pTypeDescriptor; // ref to TypeDescriptor (RTTI 0) for class
    dword numContainedBases; // count of extended classes in BaseClassArray (RTTI 2)
    struct PMD where; // member displacement structure
    dword attributes; // bit flags
    ImageBaseOffset32 pClassHierarchyDescriptor; // ref to ClassHierarchyDescriptor (RTTI 3) for class
};

typedef struct _s_IPToStateMapEntry _s_IPToStateMapEntry, *P_s_IPToStateMapEntry;

typedef struct _s_IPToStateMapEntry IPToStateMapEntry;

typedef int __ehstate_t;

struct _s_IPToStateMapEntry {
    ImageBaseOffset32 Ip;
    __ehstate_t state;
};

typedef union IMAGE_RESOURCE_DIRECTORY_ENTRY_DirectoryUnion IMAGE_RESOURCE_DIRECTORY_ENTRY_DirectoryUnion, *PIMAGE_RESOURCE_DIRECTORY_ENTRY_DirectoryUnion;

typedef struct IMAGE_RESOURCE_DIRECTORY_ENTRY_DirectoryStruct IMAGE_RESOURCE_DIRECTORY_ENTRY_DirectoryStruct, *PIMAGE_RESOURCE_DIRECTORY_ENTRY_DirectoryStruct;

struct IMAGE_RESOURCE_DIRECTORY_ENTRY_DirectoryStruct {
    dword OffsetToDirectory:31;
    dword DataIsDirectory:1;
};

union IMAGE_RESOURCE_DIRECTORY_ENTRY_DirectoryUnion {
    dword OffsetToData;
    struct IMAGE_RESOURCE_DIRECTORY_ENTRY_DirectoryStruct IMAGE_RESOURCE_DIRECTORY_ENTRY_DirectoryStruct;
};

typedef struct _s__RTTIClassHierarchyDescriptor _s__RTTIClassHierarchyDescriptor, *P_s__RTTIClassHierarchyDescriptor;

struct _s__RTTIClassHierarchyDescriptor {
    dword signature;
    dword attributes; // bit flags
    dword numBaseClasses; // number of base classes (i.e. rtti1Count)
    RTTIBaseClassDescriptor *32 __((image-base-relative)) *32 __((image-base-relative)) pBaseClassArray; // ref to BaseClassArray (RTTI 2)
};

typedef struct _s_UnwindMapEntry _s_UnwindMapEntry, *P_s_UnwindMapEntry;

struct _s_UnwindMapEntry {
    __ehstate_t toState;
    ImageBaseOffset32 action;
};

typedef struct _s__RTTICompleteObjectLocator _s__RTTICompleteObjectLocator, *P_s__RTTICompleteObjectLocator;

struct _s__RTTICompleteObjectLocator {
    dword signature;
    dword offset; // offset of vbtable within class
    dword cdOffset; // constructor displacement offset
    ImageBaseOffset32 pTypeDescriptor; // ref to TypeDescriptor (RTTI 0) for class
    ImageBaseOffset32 pClassDescriptor; // ref to ClassHierarchyDescriptor (RTTI 3)
};

typedef struct _IMAGE_RUNTIME_FUNCTION_ENTRY _IMAGE_RUNTIME_FUNCTION_ENTRY, *P_IMAGE_RUNTIME_FUNCTION_ENTRY;

struct _IMAGE_RUNTIME_FUNCTION_ENTRY {
    ImageBaseOffset32 BeginAddress;
    dword EndAddress; // Apply ImageBaseOffset32 to see reference
    ImageBaseOffset32 UnwindInfoAddressOrData;
};

typedef struct _s_UnwindMapEntry UnwindMapEntry;

typedef struct CLIENT_ID CLIENT_ID, *PCLIENT_ID;

struct CLIENT_ID {
    void *UniqueProcess;
    void *UniqueThread;
};

typedef struct _s__RTTIClassHierarchyDescriptor RTTIClassHierarchyDescriptor;

typedef struct _s_FuncInfo _s_FuncInfo, *P_s_FuncInfo;

typedef struct _s_FuncInfo FuncInfo;

struct _s_FuncInfo {
    uint magicNumber_and_bbtFlags;
    __ehstate_t maxState;
    ImageBaseOffset32 dispUnwindMap;
    uint nTryBlocks;
    ImageBaseOffset32 dispTryBlockMap;
    uint nIPMapEntries;
    ImageBaseOffset32 dispIPToStateMap;
    int dispUnwindHelp;
    ImageBaseOffset32 dispESTypeList;
    int EHFlags;
};

typedef ulonglong __uint64;

typedef struct _s__RTTICompleteObjectLocator RTTICompleteObjectLocator;

typedef struct _cpinfo _cpinfo, *P_cpinfo;

typedef uint UINT;

typedef uchar BYTE;

struct _cpinfo {
    UINT MaxCharSize;
    BYTE DefaultChar[2];
    BYTE LeadByte[12];
};

typedef int BOOL;

typedef wchar_t WCHAR;

typedef WCHAR *LPWSTR;

typedef BOOL (*LOCALE_ENUMPROCW)(LPWSTR);

typedef struct _nlsversioninfo _nlsversioninfo, *P_nlsversioninfo;

typedef struct _nlsversioninfo *LPNLSVERSIONINFO;

typedef ulong DWORD;

struct _nlsversioninfo {
    DWORD dwNLSVersionInfoSize;
    DWORD dwNLSVersion;
    DWORD dwDefinedVersion;
};

typedef struct _cpinfo *LPCPINFO;

typedef DWORD LCTYPE;

typedef struct _MEMORYSTATUSEX _MEMORYSTATUSEX, *P_MEMORYSTATUSEX;

typedef ulonglong ULONGLONG;

typedef ULONGLONG DWORDLONG;

struct _MEMORYSTATUSEX {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    DWORDLONG ullTotalPhys;
    DWORDLONG ullAvailPhys;
    DWORDLONG ullTotalPageFile;
    DWORDLONG ullAvailPageFile;
    DWORDLONG ullTotalVirtual;
    DWORDLONG ullAvailVirtual;
    DWORDLONG ullAvailExtendedVirtual;
};

typedef struct _OVERLAPPED _OVERLAPPED, *P_OVERLAPPED;

typedef ulonglong ULONG_PTR;

typedef union _union_540 _union_540, *P_union_540;

typedef void *HANDLE;

typedef struct _struct_541 _struct_541, *P_struct_541;

typedef void *PVOID;

struct _struct_541 {
    DWORD Offset;
    DWORD OffsetHigh;
};

union _union_540 {
    struct _struct_541 s;
    PVOID Pointer;
};

struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union _union_540 u;
    HANDLE hEvent;
};

typedef struct _SECURITY_ATTRIBUTES _SECURITY_ATTRIBUTES, *P_SECURITY_ATTRIBUTES;

typedef void *LPVOID;

struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
};

typedef enum _FINDEX_INFO_LEVELS {
    FindExInfoStandard=0,
    FindExInfoBasic=1,
    FindExInfoMaxInfoLevel=2
} _FINDEX_INFO_LEVELS;

typedef struct _STARTUPINFOW _STARTUPINFOW, *P_STARTUPINFOW;

typedef ushort WORD;

typedef BYTE *LPBYTE;

struct _STARTUPINFOW {
    DWORD cb;
    LPWSTR lpReserved;
    LPWSTR lpDesktop;
    LPWSTR lpTitle;
    DWORD dwX;
    DWORD dwY;
    DWORD dwXSize;
    DWORD dwYSize;
    DWORD dwXCountChars;
    DWORD dwYCountChars;
    DWORD dwFillAttribute;
    DWORD dwFlags;
    WORD wShowWindow;
    WORD cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
};

typedef struct _SYSTEMTIME _SYSTEMTIME, *P_SYSTEMTIME;

struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
};

typedef struct _STARTUPINFOW *LPSTARTUPINFOW;

typedef struct _WIN32_FIND_DATAW _WIN32_FIND_DATAW, *P_WIN32_FIND_DATAW;

typedef struct _WIN32_FIND_DATAW *LPWIN32_FIND_DATAW;

typedef struct _FILETIME _FILETIME, *P_FILETIME;

typedef struct _FILETIME FILETIME;

struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
};

struct _WIN32_FIND_DATAW {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    WCHAR cFileName[260];
    WCHAR cAlternateFileName[14];
};

typedef enum _FILE_INFO_BY_HANDLE_CLASS {
    FileBasicInfo=0,
    FileStandardInfo=1,
    FileNameInfo=2,
    FileRenameInfo=3,
    FileDispositionInfo=4,
    FileAllocationInfo=5,
    FileEndOfFileInfo=6,
    FileStreamInfo=7,
    FileCompressionInfo=8,
    FileAttributeTagInfo=9,
    FileIdBothDirectoryInfo=10,
    FileIdBothDirectoryRestartInfo=11,
    FileIoPriorityHintInfo=12,
    FileRemoteProtocolInfo=13,
    MaximumFileInfoByHandleClass=14
} _FILE_INFO_BY_HANDLE_CLASS;

typedef enum _FILE_INFO_BY_HANDLE_CLASS FILE_INFO_BY_HANDLE_CLASS;

typedef DWORD (*PTHREAD_START_ROUTINE)(LPVOID);

typedef PTHREAD_START_ROUTINE LPTHREAD_START_ROUTINE;

typedef struct _OVERLAPPED *LPOVERLAPPED;

typedef struct _PROC_THREAD_ATTRIBUTE_LIST _PROC_THREAD_ATTRIBUTE_LIST, *P_PROC_THREAD_ATTRIBUTE_LIST;

struct _PROC_THREAD_ATTRIBUTE_LIST {
};

typedef struct _MEMORYSTATUSEX *LPMEMORYSTATUSEX;

typedef enum _FINDEX_SEARCH_OPS {
    FindExSearchNameMatch=0,
    FindExSearchLimitToDirectories=1,
    FindExSearchLimitToDevices=2,
    FindExSearchMaxSearchOp=3
} _FINDEX_SEARCH_OPS;

typedef enum _FINDEX_SEARCH_OPS FINDEX_SEARCH_OPS;

typedef struct _SECURITY_ATTRIBUTES *LPSECURITY_ATTRIBUTES;

typedef struct _STARTUPINFOA _STARTUPINFOA, *P_STARTUPINFOA;

typedef char CHAR;

typedef CHAR *LPSTR;

struct _STARTUPINFOA {
    DWORD cb;
    LPSTR lpReserved;
    LPSTR lpDesktop;
    LPSTR lpTitle;
    DWORD dwX;
    DWORD dwY;
    DWORD dwXSize;
    DWORD dwYSize;
    DWORD dwXCountChars;
    DWORD dwYCountChars;
    DWORD dwFillAttribute;
    DWORD dwFlags;
    WORD wShowWindow;
    WORD cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
};

typedef struct _PROCESS_INFORMATION _PROCESS_INFORMATION, *P_PROCESS_INFORMATION;

struct _PROCESS_INFORMATION {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD dwProcessId;
    DWORD dwThreadId;
};

typedef struct _STARTUPINFOA *LPSTARTUPINFOA;

typedef enum _FINDEX_INFO_LEVELS FINDEX_INFO_LEVELS;

typedef enum _GET_FILEEX_INFO_LEVELS {
    GetFileExInfoStandard=0,
    GetFileExMaxInfoLevel=1
} _GET_FILEEX_INFO_LEVELS;

typedef struct _RTL_CONDITION_VARIABLE _RTL_CONDITION_VARIABLE, *P_RTL_CONDITION_VARIABLE;

typedef struct _RTL_CONDITION_VARIABLE RTL_CONDITION_VARIABLE;

typedef RTL_CONDITION_VARIABLE *PCONDITION_VARIABLE;

struct _RTL_CONDITION_VARIABLE {
    PVOID Ptr;
};

typedef struct _RTL_SRWLOCK _RTL_SRWLOCK, *P_RTL_SRWLOCK;

typedef struct _RTL_SRWLOCK RTL_SRWLOCK;

typedef RTL_SRWLOCK *PSRWLOCK;

struct _RTL_SRWLOCK {
    PVOID Ptr;
};

typedef struct _PROC_THREAD_ATTRIBUTE_LIST *LPPROC_THREAD_ATTRIBUTE_LIST;

typedef enum _GET_FILEEX_INFO_LEVELS GET_FILEEX_INFO_LEVELS;

typedef struct _PROCESS_INFORMATION *LPPROCESS_INFORMATION;

typedef struct _RTL_CRITICAL_SECTION _RTL_CRITICAL_SECTION, *P_RTL_CRITICAL_SECTION;

typedef struct _RTL_CRITICAL_SECTION *PRTL_CRITICAL_SECTION;

typedef PRTL_CRITICAL_SECTION LPCRITICAL_SECTION;

typedef struct _RTL_CRITICAL_SECTION_DEBUG _RTL_CRITICAL_SECTION_DEBUG, *P_RTL_CRITICAL_SECTION_DEBUG;

typedef struct _RTL_CRITICAL_SECTION_DEBUG *PRTL_CRITICAL_SECTION_DEBUG;

typedef long LONG;

typedef struct _LIST_ENTRY _LIST_ENTRY, *P_LIST_ENTRY;

typedef struct _LIST_ENTRY LIST_ENTRY;

struct _RTL_CRITICAL_SECTION {
    PRTL_CRITICAL_SECTION_DEBUG DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
};

struct _LIST_ENTRY {
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
};

struct _RTL_CRITICAL_SECTION_DEBUG {
    WORD Type;
    WORD CreatorBackTraceIndex;
    struct _RTL_CRITICAL_SECTION *CriticalSection;
    LIST_ENTRY ProcessLocksList;
    DWORD EntryCount;
    DWORD ContentionCount;
    DWORD Flags;
    WORD CreatorBackTraceIndexHigh;
    WORD SpareWORD;
};

typedef struct _CONTEXT _CONTEXT, *P_CONTEXT;

typedef struct _CONTEXT *PCONTEXT;

typedef PCONTEXT LPCONTEXT;

typedef ulonglong DWORD64;

typedef union _union_54 _union_54, *P_union_54;

typedef struct _M128A _M128A, *P_M128A;

typedef struct _M128A M128A;

typedef struct _XSAVE_FORMAT _XSAVE_FORMAT, *P_XSAVE_FORMAT;

typedef struct _XSAVE_FORMAT XSAVE_FORMAT;

typedef XSAVE_FORMAT XMM_SAVE_AREA32;

typedef struct _struct_55 _struct_55, *P_struct_55;

typedef longlong LONGLONG;

struct _M128A {
    ULONGLONG Low;
    LONGLONG High;
};

struct _XSAVE_FORMAT {
    WORD ControlWord;
    WORD StatusWord;
    BYTE TagWord;
    BYTE Reserved1;
    WORD ErrorOpcode;
    DWORD ErrorOffset;
    WORD ErrorSelector;
    WORD Reserved2;
    DWORD DataOffset;
    WORD DataSelector;
    WORD Reserved3;
    DWORD MxCsr;
    DWORD MxCsr_Mask;
    M128A FloatRegisters[8];
    M128A XmmRegisters[16];
    BYTE Reserved4[96];
};

struct _struct_55 {
    M128A Header[2];
    M128A Legacy[8];
    M128A Xmm0;
    M128A Xmm1;
    M128A Xmm2;
    M128A Xmm3;
    M128A Xmm4;
    M128A Xmm5;
    M128A Xmm6;
    M128A Xmm7;
    M128A Xmm8;
    M128A Xmm9;
    M128A Xmm10;
    M128A Xmm11;
    M128A Xmm12;
    M128A Xmm13;
    M128A Xmm14;
    M128A Xmm15;
};

union _union_54 {
    XMM_SAVE_AREA32 FltSave;
    struct _struct_55 s;
};

struct _CONTEXT {
    DWORD64 P1Home;
    DWORD64 P2Home;
    DWORD64 P3Home;
    DWORD64 P4Home;
    DWORD64 P5Home;
    DWORD64 P6Home;
    DWORD ContextFlags;
    DWORD MxCsr;
    WORD SegCs;
    WORD SegDs;
    WORD SegEs;
    WORD SegFs;
    WORD SegGs;
    WORD SegSs;
    DWORD EFlags;
    DWORD64 Dr0;
    DWORD64 Dr1;
    DWORD64 Dr2;
    DWORD64 Dr3;
    DWORD64 Dr6;
    DWORD64 Dr7;
    DWORD64 Rax;
    DWORD64 Rcx;
    DWORD64 Rdx;
    DWORD64 Rbx;
    DWORD64 Rsp;
    DWORD64 Rbp;
    DWORD64 Rsi;
    DWORD64 Rdi;
    DWORD64 R8;
    DWORD64 R9;
    DWORD64 R10;
    DWORD64 R11;
    DWORD64 R12;
    DWORD64 R13;
    DWORD64 R14;
    DWORD64 R15;
    DWORD64 Rip;
    union _union_54 u;
    M128A VectorRegister[26];
    DWORD64 VectorControl;
    DWORD64 DebugControl;
    DWORD64 LastBranchToRip;
    DWORD64 LastBranchFromRip;
    DWORD64 LastExceptionToRip;
    DWORD64 LastExceptionFromRip;
};

typedef struct _EXCEPTION_POINTERS _EXCEPTION_POINTERS, *P_EXCEPTION_POINTERS;

typedef LONG (*PTOP_LEVEL_EXCEPTION_FILTER)(struct _EXCEPTION_POINTERS *);

typedef struct _EXCEPTION_RECORD _EXCEPTION_RECORD, *P_EXCEPTION_RECORD;

typedef struct _EXCEPTION_RECORD EXCEPTION_RECORD;

typedef EXCEPTION_RECORD *PEXCEPTION_RECORD;

struct _EXCEPTION_RECORD {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    ULONG_PTR ExceptionInformation[15];
};

struct _EXCEPTION_POINTERS {
    PEXCEPTION_RECORD ExceptionRecord;
    PCONTEXT ContextRecord;
};

typedef struct _SYSTEMTIME *LPSYSTEMTIME;

typedef PTOP_LEVEL_EXCEPTION_FILTER LPTOP_LEVEL_EXCEPTION_FILTER;

typedef enum _EXCEPTION_DISPOSITION {
    ExceptionContinueExecution=0,
    ExceptionContinueSearch=1,
    ExceptionNestedException=2,
    ExceptionCollidedUnwind=3
} _EXCEPTION_DISPOSITION;

typedef enum _EXCEPTION_DISPOSITION EXCEPTION_DISPOSITION;

typedef DWORD ULONG;

typedef longlong fpos_t;

typedef struct _iobuf _iobuf, *P_iobuf;

struct _iobuf {
    char *_ptr;
    int _cnt;
    char *_base;
    int _flag;
    int _file;
    int _charbuf;
    int _bufsiz;
    char *_tmpfname;
};

typedef struct _iobuf FILE;

typedef struct _CONSOLE_READCONSOLE_CONTROL _CONSOLE_READCONSOLE_CONTROL, *P_CONSOLE_READCONSOLE_CONTROL;


// WARNING! conflicting data type names: /WinDef.h/ULONG - /wtypes.h/ULONG

struct _CONSOLE_READCONSOLE_CONTROL {
    ULONG nLength;
    ULONG nInitialChars;
    ULONG dwCtrlWakeupMask;
    ULONG dwControlKeyState;
};

typedef struct _CONSOLE_READCONSOLE_CONTROL *PCONSOLE_READCONSOLE_CONTROL;

typedef struct TypeDescriptor TypeDescriptor, *PTypeDescriptor;

struct TypeDescriptor {
    void *pVFTable;
    void *spare;
    char name[0];
};

typedef char *va_list;

typedef ulonglong uintptr_t;

typedef struct lconv lconv, *Plconv;

struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *int_curr_symbol;
    char *currency_symbol;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
    char int_frac_digits;
    char frac_digits;
    char p_cs_precedes;
    char p_sep_by_space;
    char n_cs_precedes;
    char n_sep_by_space;
    char p_sign_posn;
    char n_sign_posn;
    wchar_t *_W_decimal_point;
    wchar_t *_W_thousands_sep;
    wchar_t *_W_int_curr_symbol;
    wchar_t *_W_currency_symbol;
    wchar_t *_W_mon_decimal_point;
    wchar_t *_W_mon_thousands_sep;
    wchar_t *_W_positive_sign;
    wchar_t *_W_negative_sign;
};

typedef ushort wint_t;

typedef struct threadlocaleinfostruct threadlocaleinfostruct, *Pthreadlocaleinfostruct;

typedef struct threadlocaleinfostruct *pthreadlocinfo;

typedef struct localerefcount localerefcount, *Plocalerefcount;

typedef struct localerefcount locrefcount;

typedef struct __lc_time_data __lc_time_data, *P__lc_time_data;

struct localerefcount {
    char *locale;
    wchar_t *wlocale;
    int *refcount;
    int *wrefcount;
};

struct threadlocaleinfostruct {
    int refcount;
    uint lc_codepage;
    uint lc_collate_cp;
    uint lc_time_cp;
    locrefcount lc_category[6];
    int lc_clike;
    int mb_cur_max;
    int *lconv_intl_refcount;
    int *lconv_num_refcount;
    int *lconv_mon_refcount;
    struct lconv *lconv;
    int *ctype1_refcount;
    ushort *ctype1;
    ushort *pctype;
    uchar *pclmap;
    uchar *pcumap;
    struct __lc_time_data *lc_time_curr;
    wchar_t *locale_name[6];
};

struct __lc_time_data {
    char *wday_abbr[7];
    char *wday[7];
    char *month_abbr[12];
    char *month[12];
    char *ampm[2];
    char *ww_sdatefmt;
    char *ww_ldatefmt;
    char *ww_timefmt;
    int ww_caltype;
    int refcount;
    wchar_t *_W_wday_abbr[7];
    wchar_t *_W_wday[7];
    wchar_t *_W_month_abbr[12];
    wchar_t *_W_month[12];
    wchar_t *_W_ampm[2];
    wchar_t *_W_ww_sdatefmt;
    wchar_t *_W_ww_ldatefmt;
    wchar_t *_W_ww_timefmt;
    wchar_t *_W_ww_locale_name;
};

typedef ulonglong size_t;

typedef int errno_t;

typedef struct localeinfo_struct localeinfo_struct, *Plocaleinfo_struct;

typedef struct threadmbcinfostruct threadmbcinfostruct, *Pthreadmbcinfostruct;

typedef struct threadmbcinfostruct *pthreadmbcinfo;

struct threadmbcinfostruct {
    int refcount;
    int mbcodepage;
    int ismbcodepage;
    ushort mbulinfo[6];
    uchar mbctype[257];
    uchar mbcasemap[256];
    wchar_t *mblocalename;
};

struct localeinfo_struct {
    pthreadlocinfo locinfo;
    pthreadmbcinfo mbcinfo;
};

typedef size_t rsize_t;

typedef struct localeinfo_struct *_locale_t;

typedef ushort wctype_t;

typedef struct _MARGINS _MARGINS, *P_MARGINS;

struct _MARGINS {
    int cxLeftWidth;
    int cxRightWidth;
    int cyTopHeight;
    int cyBottomHeight;
};

typedef struct _MARGINS MARGINS;

typedef struct tagMSG tagMSG, *PtagMSG;

typedef struct tagMSG MSG;

typedef struct HWND__ HWND__, *PHWND__;

typedef struct HWND__ *HWND;

typedef ulonglong UINT_PTR;

typedef UINT_PTR WPARAM;

typedef longlong LONG_PTR;

typedef LONG_PTR LPARAM;

typedef struct tagPOINT tagPOINT, *PtagPOINT;

typedef struct tagPOINT POINT;

struct tagPOINT {
    LONG x;
    LONG y;
};

struct tagMSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
};

struct HWND__ {
    int unused;
};

typedef struct tagTRACKMOUSEEVENT tagTRACKMOUSEEVENT, *PtagTRACKMOUSEEVENT;

struct tagTRACKMOUSEEVENT {
    DWORD cbSize;
    DWORD dwFlags;
    HWND hwndTrack;
    DWORD dwHoverTime;
};

typedef struct tagMSG *LPMSG;

typedef struct tagWNDCLASSEXW tagWNDCLASSEXW, *PtagWNDCLASSEXW;

typedef struct tagWNDCLASSEXW WNDCLASSEXW;

typedef LONG_PTR LRESULT;

typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct HINSTANCE__ HINSTANCE__, *PHINSTANCE__;

typedef struct HINSTANCE__ *HINSTANCE;

typedef struct HICON__ HICON__, *PHICON__;

typedef struct HICON__ *HICON;

typedef HICON HCURSOR;

typedef struct HBRUSH__ HBRUSH__, *PHBRUSH__;

typedef struct HBRUSH__ *HBRUSH;

typedef WCHAR *LPCWSTR;

struct HBRUSH__ {
    int unused;
};

struct HICON__ {
    int unused;
};

struct HINSTANCE__ {
    int unused;
};

struct tagWNDCLASSEXW {
    UINT cbSize;
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCWSTR lpszMenuName;
    LPCWSTR lpszClassName;
    HICON hIconSm;
};

typedef struct tagTRACKMOUSEEVENT *LPTRACKMOUSEEVENT;

typedef struct exception exception, *Pexception;

struct exception { // PlaceHolder Class Structure
};

typedef struct basic_streambuf<char,struct_std::char_traits<char>_> basic_streambuf<char,struct_std::char_traits<char>_>, *Pbasic_streambuf<char,struct_std::char_traits<char>_>;

struct basic_streambuf<char,struct_std::char_traits<char>_> { // PlaceHolder Class Structure
};

typedef struct ios_base ios_base, *Pios_base;

struct ios_base { // PlaceHolder Class Structure
};

typedef struct ctype<char> ctype<char>, *Pctype<char>;

struct ctype<char> { // PlaceHolder Class Structure
};

typedef struct basic_filebuf<char,struct_std::char_traits<char>_> basic_filebuf<char,struct_std::char_traits<char>_>, *Pbasic_filebuf<char,struct_std::char_traits<char>_>;

struct basic_filebuf<char,struct_std::char_traits<char>_> { // PlaceHolder Class Structure
};

typedef struct failure failure, *Pfailure;

struct failure { // PlaceHolder Class Structure
};


// WARNING! conflicting data type names: /guiddef.h/GUID - /GUID

typedef GUID IID;

typedef struct _GUID _GUID, *P_GUID;

struct _GUID {
    ulong Data1;
    ushort Data2;
    ushort Data3;
    uchar Data4[8];
};

typedef PVOID BCRYPT_KEY_HANDLE;

typedef PVOID BCRYPT_HANDLE;

typedef PVOID BCRYPT_ALG_HANDLE;

typedef PVOID BCRYPT_HASH_HANDLE;

typedef LONG NTSTATUS;

typedef struct SC_HANDLE__ SC_HANDLE__, *PSC_HANDLE__;

typedef struct SC_HANDLE__ *SC_HANDLE;

struct SC_HANDLE__ {
    int unused;
};

typedef struct _CONTEXT CONTEXT;

typedef union _LARGE_INTEGER _LARGE_INTEGER, *P_LARGE_INTEGER;

typedef struct _struct_19 _struct_19, *P_struct_19;

typedef struct _struct_20 _struct_20, *P_struct_20;

struct _struct_20 {
    DWORD LowPart;
    LONG HighPart;
};

struct _struct_19 {
    DWORD LowPart;
    LONG HighPart;
};

union _LARGE_INTEGER {
    struct _struct_19 s;
    struct _struct_20 u;
    LONGLONG QuadPart;
};

typedef union _LARGE_INTEGER LARGE_INTEGER;

typedef struct _RUNTIME_FUNCTION _RUNTIME_FUNCTION, *P_RUNTIME_FUNCTION;

struct _RUNTIME_FUNCTION {
    DWORD BeginAddress;
    DWORD EndAddress;
    DWORD UnwindData;
};

typedef struct _RUNTIME_FUNCTION *PRUNTIME_FUNCTION;

typedef EXCEPTION_DISPOSITION (EXCEPTION_ROUTINE)(struct _EXCEPTION_RECORD *, PVOID, struct _CONTEXT *, PVOID);

typedef WCHAR *LPWCH;

typedef struct _M128A *PM128A;

typedef struct _UNWIND_HISTORY_TABLE_ENTRY _UNWIND_HISTORY_TABLE_ENTRY, *P_UNWIND_HISTORY_TABLE_ENTRY;

typedef struct _UNWIND_HISTORY_TABLE_ENTRY UNWIND_HISTORY_TABLE_ENTRY;

struct _UNWIND_HISTORY_TABLE_ENTRY {
    DWORD64 ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
};

typedef union _union_61 _union_61, *P_union_61;

typedef struct _struct_62 _struct_62, *P_struct_62;

struct _struct_62 {
    PM128A Xmm0;
    PM128A Xmm1;
    PM128A Xmm2;
    PM128A Xmm3;
    PM128A Xmm4;
    PM128A Xmm5;
    PM128A Xmm6;
    PM128A Xmm7;
    PM128A Xmm8;
    PM128A Xmm9;
    PM128A Xmm10;
    PM128A Xmm11;
    PM128A Xmm12;
    PM128A Xmm13;
    PM128A Xmm14;
    PM128A Xmm15;
};

union _union_61 {
    PM128A FloatingContext[16];
    struct _struct_62 s;
};

typedef union _union_63 _union_63, *P_union_63;

typedef ulonglong *PDWORD64;

typedef struct _struct_64 _struct_64, *P_struct_64;

struct _struct_64 {
    PDWORD64 Rax;
    PDWORD64 Rcx;
    PDWORD64 Rdx;
    PDWORD64 Rbx;
    PDWORD64 Rsp;
    PDWORD64 Rbp;
    PDWORD64 Rsi;
    PDWORD64 Rdi;
    PDWORD64 R8;
    PDWORD64 R9;
    PDWORD64 R10;
    PDWORD64 R11;
    PDWORD64 R12;
    PDWORD64 R13;
    PDWORD64 R14;
    PDWORD64 R15;
};

union _union_63 {
    PDWORD64 IntegerContext[16];
    struct _struct_64 s;
};

typedef struct _UNWIND_HISTORY_TABLE _UNWIND_HISTORY_TABLE, *P_UNWIND_HISTORY_TABLE;

typedef struct _UNWIND_HISTORY_TABLE *PUNWIND_HISTORY_TABLE;

struct _UNWIND_HISTORY_TABLE {
    DWORD Count;
    BYTE LocalHint;
    BYTE GlobalHint;
    BYTE Search;
    BYTE Once;
    DWORD64 LowAddress;
    DWORD64 HighAddress;
    UNWIND_HISTORY_TABLE_ENTRY Entry[12];
};

typedef long HRESULT;

typedef CHAR *LPCSTR;

typedef CHAR *LPCH;

typedef void (*PFLS_CALLBACK_FUNCTION)(PVOID);

typedef LARGE_INTEGER *PLARGE_INTEGER;

typedef struct _KNONVOLATILE_CONTEXT_POINTERS _KNONVOLATILE_CONTEXT_POINTERS, *P_KNONVOLATILE_CONTEXT_POINTERS;

typedef struct _KNONVOLATILE_CONTEXT_POINTERS *PKNONVOLATILE_CONTEXT_POINTERS;

struct _KNONVOLATILE_CONTEXT_POINTERS {
    union _union_61 u;
    union _union_63 u2;
};

typedef EXCEPTION_ROUTINE *PEXCEPTION_ROUTINE;

typedef DWORD ACCESS_MASK;

typedef short SHORT;

typedef DWORD LCID;

typedef struct _CRYPTOAPI_BLOB _CRYPTOAPI_BLOB, *P_CRYPTOAPI_BLOB;

typedef struct _CRYPTOAPI_BLOB DATA_BLOB;

struct _CRYPTOAPI_BLOB {
    DWORD cbData;
    BYTE *pbData;
};

typedef struct _CRYPTPROTECT_PROMPTSTRUCT _CRYPTPROTECT_PROMPTSTRUCT, *P_CRYPTPROTECT_PROMPTSTRUCT;

typedef struct _CRYPTPROTECT_PROMPTSTRUCT CRYPTPROTECT_PROMPTSTRUCT;

struct _CRYPTPROTECT_PROMPTSTRUCT {
    DWORD cbSize;
    DWORD dwPromptFlags;
    HWND hwndApp;
    LPCWSTR szPrompt;
};

typedef struct IMAGE_DOS_HEADER IMAGE_DOS_HEADER, *PIMAGE_DOS_HEADER;

struct IMAGE_DOS_HEADER {
    char e_magic[2]; // Magic number
    word e_cblp; // Bytes of last page
    word e_cp; // Pages in file
    word e_crlc; // Relocations
    word e_cparhdr; // Size of header in paragraphs
    word e_minalloc; // Minimum extra paragraphs needed
    word e_maxalloc; // Maximum extra paragraphs needed
    word e_ss; // Initial (relative) SS value
    word e_sp; // Initial SP value
    word e_csum; // Checksum
    word e_ip; // Initial IP value
    word e_cs; // Initial (relative) CS value
    word e_lfarlc; // File address of relocation table
    word e_ovno; // Overlay number
    word e_res[4][4]; // Reserved words
    word e_oemid; // OEM identifier (for e_oeminfo)
    word e_oeminfo; // OEM information; e_oemid specific
    word e_res2[10][10]; // Reserved words
    dword e_lfanew; // File address of new exe header
    byte e_program[64]; // Actual DOS program
};

typedef ULONG_PTR DWORD_PTR;

typedef ULONG_PTR *PSIZE_T;

typedef ULONG_PTR SIZE_T;

typedef longlong INT_PTR;

typedef struct HKL__ HKL__, *PHKL__;

struct HKL__ {
    int unused;
};

typedef struct tagPOINT *LPPOINT;

typedef struct HKEY__ HKEY__, *PHKEY__;

struct HKEY__ {
    int unused;
};

typedef uchar UCHAR;

typedef UCHAR *PUCHAR;

typedef DWORD *LPDWORD;

typedef DWORD *PDWORD;

typedef struct HDC__ HDC__, *PHDC__;

struct HDC__ {
    int unused;
};

typedef struct HKL__ *HKL;

typedef HINSTANCE HMODULE;

typedef HANDLE HLOCAL;

typedef BOOL *PBOOL;

typedef struct HMENU__ HMENU__, *PHMENU__;

typedef struct HMENU__ *HMENU;

struct HMENU__ {
    int unused;
};

typedef struct _FILETIME *LPFILETIME;

typedef struct tagRECT tagRECT, *PtagRECT;

typedef struct tagRECT RECT;

struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
};

typedef INT_PTR (*FARPROC)(void);

typedef struct HDC__ *HDC;

typedef WORD *LPWORD;

typedef struct HKEY__ *HKEY;

typedef HKEY *PHKEY;

typedef int INT;

typedef WORD ATOM;

typedef struct tagRECT *LPRECT;

typedef HANDLE HGLOBAL;

typedef BOOL *LPBOOL;

typedef void *LPCVOID;

typedef struct IMAGE_RESOURCE_DIRECTORY_ENTRY_NameStruct IMAGE_RESOURCE_DIRECTORY_ENTRY_NameStruct, *PIMAGE_RESOURCE_DIRECTORY_ENTRY_NameStruct;

struct IMAGE_RESOURCE_DIRECTORY_ENTRY_NameStruct {
    dword NameOffset:31;
    dword NameIsString:1;
};

typedef struct IMAGE_LOAD_CONFIG_CODE_INTEGRITY IMAGE_LOAD_CONFIG_CODE_INTEGRITY, *PIMAGE_LOAD_CONFIG_CODE_INTEGRITY;

struct IMAGE_LOAD_CONFIG_CODE_INTEGRITY {
    word Flags;
    word Catalog;
    dword CatalogOffset;
    dword Reserved;
};

typedef struct IMAGE_DEBUG_DIRECTORY IMAGE_DEBUG_DIRECTORY, *PIMAGE_DEBUG_DIRECTORY;

struct IMAGE_DEBUG_DIRECTORY {
    dword Characteristics;
    dword TimeDateStamp;
    word MajorVersion;
    word MinorVersion;
    dword Type;
    dword SizeOfData;
    dword AddressOfRawData;
    dword PointerToRawData;
};

typedef struct IMAGE_FILE_HEADER IMAGE_FILE_HEADER, *PIMAGE_FILE_HEADER;

struct IMAGE_FILE_HEADER {
    word Machine; // 34404
    word NumberOfSections;
    dword TimeDateStamp;
    dword PointerToSymbolTable;
    dword NumberOfSymbols;
    word SizeOfOptionalHeader;
    word Characteristics;
};

typedef struct IMAGE_LOAD_CONFIG_DIRECTORY64 IMAGE_LOAD_CONFIG_DIRECTORY64, *PIMAGE_LOAD_CONFIG_DIRECTORY64;

typedef enum IMAGE_GUARD_FLAGS {
    IMAGE_GUARD_CF_INSTRUMENTED=256,
    IMAGE_GUARD_CFW_INSTRUMENTED=512,
    IMAGE_GUARD_CF_FUNCTION_TABLE_PRESENT=1024,
    IMAGE_GUARD_SECURITY_COOKIE_UNUSED=2048,
    IMAGE_GUARD_PROTECT_DELAYLOAD_IAT=4096,
    IMAGE_GUARD_DELAYLOAD_IAT_IN_ITS_OWN_SECTION=8192,
    IMAGE_GUARD_CF_EXPORT_SUPPRESSION_INFO_PRESENT=16384,
    IMAGE_GUARD_CF_ENABLE_EXPORT_SUPPRESSION=32768,
    IMAGE_GUARD_CF_LONGJUMP_TABLE_PRESENT=65536,
    IMAGE_GUARD_RF_INSTRUMENTED=131072,
    IMAGE_GUARD_RF_ENABLE=262144,
    IMAGE_GUARD_RF_STRICT=524288,
    IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_MASK_1=268435456,
    IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_MASK_2=536870912,
    IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_MASK_4=1073741824,
    IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_MASK_8=2147483648
} IMAGE_GUARD_FLAGS;

struct IMAGE_LOAD_CONFIG_DIRECTORY64 {
    dword Size;
    dword TimeDateStamp;
    word MajorVersion;
    word MinorVersion;
    dword GlobalFlagsClear;
    dword GlobalFlagsSet;
    dword CriticalSectionDefaultTimeout;
    qword DeCommitFreeBlockThreshold;
    qword DeCommitTotalFreeThreshold;
    pointer64 LockPrefixTable;
    qword MaximumAllocationSize;
    qword VirtualMemoryThreshold;
    qword ProcessAffinityMask;
    dword ProcessHeapFlags;
    word CsdVersion;
    word DependentLoadFlags;
    pointer64 EditList;
    pointer64 SecurityCookie;
    pointer64 SEHandlerTable;
    qword SEHandlerCount;
    pointer64 GuardCFCCheckFunctionPointer;
    pointer64 GuardCFDispatchFunctionPointer;
    pointer64 GuardCFFunctionTable;
    qword GuardCFFunctionCount;
    enum IMAGE_GUARD_FLAGS GuardFlags;
    struct IMAGE_LOAD_CONFIG_CODE_INTEGRITY CodeIntegrity;
    pointer64 GuardAddressTakenIatEntryTable;
    qword GuardAddressTakenIatEntryCount;
    pointer64 GuardLongJumpTargetTable;
    qword GuardLongJumpTargetCount;
    pointer64 DynamicValueRelocTable;
    pointer64 CHPEMetadataPointer;
    pointer64 GuardRFFailureRoutine;
    pointer64 GuardRFFailureRoutineFunctionPointer;
    dword DynamicValueRelocTableOffset;
    word DynamicValueRelocTableSection;
    word Reserved1;
    pointer64 GuardRFVerifyStackPointerFunctionPointer;
    dword HotPatchTableOffset;
    dword Reserved2;
    qword Reserved3;
};

typedef struct IMAGE_RESOURCE_DIRECTORY_ENTRY IMAGE_RESOURCE_DIRECTORY_ENTRY, *PIMAGE_RESOURCE_DIRECTORY_ENTRY;

typedef union IMAGE_RESOURCE_DIRECTORY_ENTRY_NameUnion IMAGE_RESOURCE_DIRECTORY_ENTRY_NameUnion, *PIMAGE_RESOURCE_DIRECTORY_ENTRY_NameUnion;

union IMAGE_RESOURCE_DIRECTORY_ENTRY_NameUnion {
    struct IMAGE_RESOURCE_DIRECTORY_ENTRY_NameStruct IMAGE_RESOURCE_DIRECTORY_ENTRY_NameStruct;
    dword Name;
    word Id;
};

struct IMAGE_RESOURCE_DIRECTORY_ENTRY {
    union IMAGE_RESOURCE_DIRECTORY_ENTRY_NameUnion NameUnion;
    union IMAGE_RESOURCE_DIRECTORY_ENTRY_DirectoryUnion DirectoryUnion;
};

typedef struct IMAGE_OPTIONAL_HEADER64 IMAGE_OPTIONAL_HEADER64, *PIMAGE_OPTIONAL_HEADER64;

typedef struct IMAGE_DATA_DIRECTORY IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;

struct IMAGE_DATA_DIRECTORY {
    ImageBaseOffset32 VirtualAddress;
    dword Size;
};

struct IMAGE_OPTIONAL_HEADER64 {
    word Magic;
    byte MajorLinkerVersion;
    byte MinorLinkerVersion;
    dword SizeOfCode;
    dword SizeOfInitializedData;
    dword SizeOfUninitializedData;
    ImageBaseOffset32 AddressOfEntryPoint;
    ImageBaseOffset32 BaseOfCode;
    pointer64 ImageBase;
    dword SectionAlignment;
    dword FileAlignment;
    word MajorOperatingSystemVersion;
    word MinorOperatingSystemVersion;
    word MajorImageVersion;
    word MinorImageVersion;
    word MajorSubsystemVersion;
    word MinorSubsystemVersion;
    dword Win32VersionValue;
    dword SizeOfImage;
    dword SizeOfHeaders;
    dword CheckSum;
    word Subsystem;
    word DllCharacteristics;
    qword SizeOfStackReserve;
    qword SizeOfStackCommit;
    qword SizeOfHeapReserve;
    qword SizeOfHeapCommit;
    dword LoaderFlags;
    dword NumberOfRvaAndSizes;
    struct IMAGE_DATA_DIRECTORY DataDirectory[16];
};

typedef struct IMAGE_SECTION_HEADER IMAGE_SECTION_HEADER, *PIMAGE_SECTION_HEADER;

typedef union Misc Misc, *PMisc;

typedef enum SectionFlags {
    IMAGE_SCN_TYPE_NO_PAD=8,
    IMAGE_SCN_RESERVED_0001=16,
    IMAGE_SCN_CNT_CODE=32,
    IMAGE_SCN_CNT_INITIALIZED_DATA=64,
    IMAGE_SCN_CNT_UNINITIALIZED_DATA=128,
    IMAGE_SCN_LNK_OTHER=256,
    IMAGE_SCN_LNK_INFO=512,
    IMAGE_SCN_RESERVED_0040=1024,
    IMAGE_SCN_LNK_REMOVE=2048,
    IMAGE_SCN_LNK_COMDAT=4096,
    IMAGE_SCN_GPREL=32768,
    IMAGE_SCN_MEM_16BIT=131072,
    IMAGE_SCN_MEM_PURGEABLE=131072,
    IMAGE_SCN_MEM_LOCKED=262144,
    IMAGE_SCN_MEM_PRELOAD=524288,
    IMAGE_SCN_ALIGN_1BYTES=1048576,
    IMAGE_SCN_ALIGN_2BYTES=2097152,
    IMAGE_SCN_ALIGN_4BYTES=3145728,
    IMAGE_SCN_ALIGN_8BYTES=4194304,
    IMAGE_SCN_ALIGN_16BYTES=5242880,
    IMAGE_SCN_ALIGN_32BYTES=6291456,
    IMAGE_SCN_ALIGN_64BYTES=7340032,
    IMAGE_SCN_ALIGN_128BYTES=8388608,
    IMAGE_SCN_ALIGN_256BYTES=9437184,
    IMAGE_SCN_ALIGN_512BYTES=10485760,
    IMAGE_SCN_ALIGN_1024BYTES=11534336,
    IMAGE_SCN_ALIGN_2048BYTES=12582912,
    IMAGE_SCN_ALIGN_4096BYTES=13631488,
    IMAGE_SCN_ALIGN_8192BYTES=14680064,
    IMAGE_SCN_LNK_NRELOC_OVFL=16777216,
    IMAGE_SCN_MEM_DISCARDABLE=33554432,
    IMAGE_SCN_MEM_NOT_CACHED=67108864,
    IMAGE_SCN_MEM_NOT_PAGED=134217728,
    IMAGE_SCN_MEM_SHARED=268435456,
    IMAGE_SCN_MEM_EXECUTE=536870912,
    IMAGE_SCN_MEM_READ=1073741824,
    IMAGE_SCN_MEM_WRITE=2147483648
} SectionFlags;

union Misc {
    dword PhysicalAddress;
    dword VirtualSize;
};

struct IMAGE_SECTION_HEADER {
    char Name[8];
    union Misc Misc;
    ImageBaseOffset32 VirtualAddress;
    dword SizeOfRawData;
    dword PointerToRawData;
    dword PointerToRelocations;
    dword PointerToLinenumbers;
    word NumberOfRelocations;
    word NumberOfLinenumbers;
    enum SectionFlags Characteristics;
};

typedef struct IMAGE_NT_HEADERS64 IMAGE_NT_HEADERS64, *PIMAGE_NT_HEADERS64;

struct IMAGE_NT_HEADERS64 {
    char Signature[4];
    struct IMAGE_FILE_HEADER FileHeader;
    struct IMAGE_OPTIONAL_HEADER64 OptionalHeader;
};

typedef struct IMAGE_THUNK_DATA64 IMAGE_THUNK_DATA64, *PIMAGE_THUNK_DATA64;

struct IMAGE_THUNK_DATA64 {
    qword StartAddressOfRawData;
    qword EndAddressOfRawData;
    qword AddressOfIndex;
    qword AddressOfCallBacks;
    dword SizeOfZeroFill;
    dword Characteristics;
};

typedef struct IMAGE_RESOURCE_DATA_ENTRY IMAGE_RESOURCE_DATA_ENTRY, *PIMAGE_RESOURCE_DATA_ENTRY;

struct IMAGE_RESOURCE_DATA_ENTRY {
    dword OffsetToData;
    dword Size;
    dword CodePage;
    dword Reserved;
};

typedef struct IMAGE_RESOURCE_DIRECTORY IMAGE_RESOURCE_DIRECTORY, *PIMAGE_RESOURCE_DIRECTORY;

struct IMAGE_RESOURCE_DIRECTORY {
    dword Characteristics;
    dword TimeDateStamp;
    word MajorVersion;
    word MinorVersion;
    word NumberOfNamedEntries;
    word NumberOfIdEntries;
};

typedef LONG LSTATUS;

typedef ACCESS_MASK REGSAM;

typedef struct HIMC__ HIMC__, *PHIMC__;

struct HIMC__ {
    int unused;
};

typedef struct tagCOMPOSITIONFORM tagCOMPOSITIONFORM, *PtagCOMPOSITIONFORM;

struct tagCOMPOSITIONFORM {
    DWORD dwStyle;
    POINT ptCurrentPos;
    RECT rcArea;
};

typedef struct tagCOMPOSITIONFORM *LPCOMPOSITIONFORM;

typedef struct HIMC__ *HIMC;

typedef struct tagCANDIDATEFORM tagCANDIDATEFORM, *PtagCANDIDATEFORM;

struct tagCANDIDATEFORM {
    DWORD dwIndex;
    DWORD dwStyle;
    POINT ptCurrentPos;
    RECT rcArea;
};

typedef struct tagCANDIDATEFORM *LPCANDIDATEFORM;

typedef struct __crt_multibyte_data __crt_multibyte_data, *P__crt_multibyte_data;

struct __crt_multibyte_data { // PlaceHolder Structure
};

typedef struct __acrt_ptd __acrt_ptd, *P__acrt_ptd;

struct __acrt_ptd { // PlaceHolder Structure
};

typedef struct __crt_locale_pointers __crt_locale_pointers, *P__crt_locale_pointers;

struct __crt_locale_pointers { // PlaceHolder Structure
};

typedef struct <lambda_8dff2cf36a5417162780cd64fa2883ef> <lambda_8dff2cf36a5417162780cd64fa2883ef>, *P<lambda_8dff2cf36a5417162780cd64fa2883ef>;

struct <lambda_8dff2cf36a5417162780cd64fa2883ef> { // PlaceHolder Structure
};

typedef struct <lambda_2a444430fde8c29194d880d93eed5e8f> <lambda_2a444430fde8c29194d880d93eed5e8f>, *P<lambda_2a444430fde8c29194d880d93eed5e8f>;

struct <lambda_2a444430fde8c29194d880d93eed5e8f> { // PlaceHolder Structure
};

typedef struct <lambda_3e16ef9562a7dcce91392c22ab16ea36> <lambda_3e16ef9562a7dcce91392c22ab16ea36>, *P<lambda_3e16ef9562a7dcce91392c22ab16ea36>;

struct <lambda_3e16ef9562a7dcce91392c22ab16ea36> { // PlaceHolder Structure
};

typedef struct <lambda_7f2adfce497ff2baa965cd4f576ecfd1> <lambda_7f2adfce497ff2baa965cd4f576ecfd1>, *P<lambda_7f2adfce497ff2baa965cd4f576ecfd1>;

struct <lambda_7f2adfce497ff2baa965cd4f576ecfd1> { // PlaceHolder Structure
};

typedef struct __crt_seh_guarded_call<void> __crt_seh_guarded_call<void>, *P__crt_seh_guarded_call<void>;

struct __crt_seh_guarded_call<void> { // PlaceHolder Structure
};

typedef struct EHExceptionRecord EHExceptionRecord, *PEHExceptionRecord;

struct EHExceptionRecord { // PlaceHolder Structure
};

typedef struct _LocaleUpdate _LocaleUpdate, *P_LocaleUpdate;

struct _LocaleUpdate { // PlaceHolder Structure
};

typedef struct __acrt_stdio_stream_mode __acrt_stdio_stream_mode, *P__acrt_stdio_stream_mode;

struct __acrt_stdio_stream_mode { // PlaceHolder Structure
};

typedef struct __crt_locale_data __crt_locale_data, *P__crt_locale_data;

struct __crt_locale_data { // PlaceHolder Structure
};

typedef struct <lambda_410d79af7f07d98d83a3f525b3859a53> <lambda_410d79af7f07d98d83a3f525b3859a53>, *P<lambda_410d79af7f07d98d83a3f525b3859a53>;

struct <lambda_410d79af7f07d98d83a3f525b3859a53> { // PlaceHolder Structure
};

typedef struct __crt_deferred_errno_cache __crt_deferred_errno_cache, *P__crt_deferred_errno_cache;

struct __crt_deferred_errno_cache { // PlaceHolder Structure
};

typedef enum SLD_STATUS {
} SLD_STATUS;

typedef struct <lambda_38119f0e861e05405d8a144b9b982f0a> <lambda_38119f0e861e05405d8a144b9b982f0a>, *P<lambda_38119f0e861e05405d8a144b9b982f0a>;

struct <lambda_38119f0e861e05405d8a144b9b982f0a> { // PlaceHolder Structure
};

typedef enum __acrt_rounding_mode {
} __acrt_rounding_mode;

typedef struct __crt_stdio_stream __crt_stdio_stream, *P__crt_stdio_stream;

struct __crt_stdio_stream { // PlaceHolder Structure
};

typedef struct __crt_lc_time_data __crt_lc_time_data, *P__crt_lc_time_data;

struct __crt_lc_time_data { // PlaceHolder Structure
};

typedef struct error_code error_code, *Perror_code;

struct error_code { // PlaceHolder Structure
};

typedef struct basic_stringstream<char,struct_std::char_traits<char>,class_std::allocator<char>_> basic_stringstream<char,struct_std::char_traits<char>,class_std::allocator<char>_>, *Pbasic_stringstream<char,struct_std::char_traits<char>,class_std::allocator<char>_>;

struct basic_stringstream<char,struct_std::char_traits<char>,class_std::allocator<char>_> { // PlaceHolder Structure
};

typedef struct input_processor<char,class___crt_stdio_input::string_input_adapter<char>_> input_processor<char,class___crt_stdio_input::string_input_adapter<char>_>, *Pinput_processor<char,class___crt_stdio_input::string_input_adapter<char>_>;

struct input_processor<char,class___crt_stdio_input::string_input_adapter<char>_> { // PlaceHolder Structure
};

typedef enum conversion_mode {
} conversion_mode;

typedef struct string_output_adapter<char> string_output_adapter<char>, *Pstring_output_adapter<char>;

struct string_output_adapter<char> { // PlaceHolder Structure
};

typedef struct floating_point_string floating_point_string, *Pfloating_point_string;

struct floating_point_string { // PlaceHolder Structure
};

typedef struct floating_point_value floating_point_value, *Pfloating_point_value;

struct floating_point_value { // PlaceHolder Structure
};

typedef struct IUnknownVtbl IUnknownVtbl, *PIUnknownVtbl;

typedef struct IUnknown IUnknown, *PIUnknown;

struct IUnknownVtbl {
    HRESULT (*QueryInterface)(struct IUnknown *, IID *, void **);
    ULONG (*AddRef)(struct IUnknown *);
    ULONG (*Release)(struct IUnknown *);
};

struct IUnknown {
    struct IUnknownVtbl *lpVtbl;
};

typedef struct IUnknown *LPUNKNOWN;




void FUN_140001048(void);
void FUN_1400010c0(void);
void FUN_140001138(void);
void FUN_14000117c(void);
void FUN_1400011c0(void);
void FUN_140001210(void);
void FUN_140001250(void);
void FUN_140001460(void);
void FUN_1400014b0(void);
undefined * FUN_1400014d0(void);
undefined * FUN_1400014d8(void);
void FUN_1400014e0(longlong param_1,longlong param_2,undefined8 param_3,undefined8 param_4);
ulonglong FUN_140001524(undefined1 *param_1,ulonglong param_2,longlong param_3,undefined8 param_4);
ulonglong FUN_140001580(undefined1 *param_1,ulonglong param_2,ulonglong param_3,longlong param_4,undefined1 param_5);
void FUN_1400015e0(undefined1 (*param_1) [32],longlong param_2,undefined8 param_3,undefined8 param_4);
exception * __thiscall std::exception::exception(exception *this,exception *param_1);
char * FUN_140001668(longlong param_1);
undefined8 * FUN_140001680(undefined8 *param_1,uint param_2);
undefined8 * FUN_1400016e4(undefined8 *param_1);
void FUN_140001710(void);
undefined8 * FUN_140001730(undefined8 *param_1,longlong param_2);
undefined8 * FUN_14000176c(undefined8 *param_1,longlong param_2);
void FUN_1400017a8(void);
undefined8 * FUN_1400017bc(undefined8 *param_1,longlong param_2);
undefined4 * FUN_1400017f8(undefined8 param_1,undefined4 *param_2,undefined4 param_3);
longlong FUN_140001804(longlong *param_1,undefined4 param_2,int *param_3);
longlong FUN_140001840(longlong param_1,int *param_2,int param_3);
undefined4 * FUN_14000185c(undefined4 *param_1,undefined4 param_2);
longlong * FUN_140001870(longlong *param_1,undefined4 *param_2,longlong *param_3);
undefined8 * FUN_14000193c(undefined8 *param_1,undefined8 *param_2,undefined8 *param_3);
undefined8 * FUN_140001a0c(undefined8 *param_1,uint param_2);
undefined8 * FUN_140001a50(undefined8 *param_1,undefined8 *param_2,char *param_3);
undefined8 * FUN_140001b14(undefined8 *param_1,longlong param_2);
undefined8 * FUN_140001b6c(undefined8 *param_1,longlong param_2);
char * FUN_140001bbc(void);
undefined8 * FUN_140001bc4(undefined8 param_1,undefined8 *param_2,int param_3);
undefined8 FUN_140001c44(undefined8 param_1,ulonglong param_2);
undefined8 * FUN_140001c68(undefined8 *param_1);
void FUN_140001c8c(void);
undefined8 * FUN_140001cac(undefined8 *param_1,longlong param_2);
undefined8 * FUN_140001ce8(undefined8 *param_1,ulonglong param_2);
int * FUN_140001d14(int *param_1,char *param_2);
void FUN_140001d8c(int *param_1);
void FUN_140001e2c(longlong param_1);
longlong FUN_140001e34(longlong param_1);
void FUN_140001e50(longlong param_1);
undefined8 FUN_140001e7c(longlong *param_1,longlong param_2);
void FUN_140001f30(longlong param_1,byte param_2);
byte * FUN_140001f40(longlong param_1,byte *param_2,byte *param_3);
void FUN_140001f88(longlong param_1,byte param_2);
byte * FUN_140001f98(longlong param_1,byte *param_2,byte *param_3);
undefined1 FUN_140001fe0(undefined8 param_1,undefined1 param_2);
longlong FUN_140001fe4(undefined8 param_1,undefined8 *param_2,longlong param_3,undefined8 *param_4);
longlong FUN_140002004(undefined8 param_1,undefined8 *param_2,longlong param_3,undefined8 param_4,undefined8 *param_5);
void * __thiscall std::ctype<char>::`scalar_deleting_destructor'(ctype<char> *this,uint param_1);
failure * __thiscall std::ios_base::failure::failure(failure *this,char *param_1,error_code *param_2);
void FUN_1400020c4(longlong param_1,uint param_2,char param_3);
void FUN_140002148(longlong param_1);
undefined8 * FUN_1400021b0(undefined8 *param_1,longlong param_2);
void * __thiscall std::ios_base::`scalar_deleting_destructor'(ios_base *this,uint param_1);
void FUN_140002248(longlong param_1);
undefined8 * FUN_140002260(void);
void FUN_1400023f4(undefined8 *param_1);
void FUN_140002870(longlong param_1);
void FUN_14000299c(longlong param_1);
void FUN_1400029cc(longlong param_1);
void FUN_140002a00(longlong *param_1,longlong *param_2,undefined8 *param_3);
float * FUN_140002a4c(float *param_1,undefined8 param_2,undefined8 param_3,float param_4,float param_5);
bool FUN_140002ca0(undefined8 param_1,undefined8 param_2,float *param_3,ulonglong param_4);
bool FUN_140002eec(float *param_1,undefined8 param_2,float *param_3,ulonglong param_4);
void FUN_140002f98(float param_1,float param_2,uint param_3);
void FUN_140003114(byte *param_1,longlong param_2,undefined8 *param_3);
void FUN_1400031dc(void);
void FUN_1400034e4(void);
void FUN_140003ca0(undefined8 param_1,byte *param_2);
undefined1 * FUN_140003ddc(undefined1 *param_1,undefined1 *param_2);
undefined8 * FUN_140003ec0(undefined8 *param_1,undefined1 (*param_2) [32]);
longlong * FUN_140003ff0(longlong *param_1,undefined8 *param_2);
void FUN_140004198(undefined8 param_1,float param_2);
void FUN_140004508(undefined8 *param_1);
void FUN_140004994(byte *param_1);
void FUN_140005e34(undefined8 param_1,LPCSTR param_2,LPCSTR param_3);
void thunk_FUN_14000fd50(longlong *param_1);
void FUN_140005ec0(longlong *param_1);
void FUN_140005ef4(longlong param_1);
void FUN_140005f00(longlong *param_1);
void FUN_140005f28(uint param_1,undefined8 *param_2,undefined8 param_3,longlong param_4,float param_5,float param_6,char param_7);
void FUN_140006730(void);
void FUN_140006f30(void);
void FUN_14000791c(longlong *param_1);
undefined8 * FUN_14000bd68(undefined8 param_1,undefined8 *param_2);
undefined8 * FUN_14000be04(undefined8 param_1,undefined8 *param_2);
undefined8 * FUN_14000be98(undefined8 param_1,undefined8 *param_2);
void FUN_14000bf2c(void);
bool FUN_14000c030(undefined8 param_1);
void FUN_14000c114(void);
void FUN_14000c194(void);
longlong FUN_14000c1e0(HWND param_1,uint param_2,ulonglong param_3,ulonglong param_4);
longlong * FUN_14000d458(longlong param_1);
undefined8 * FUN_14000d568(undefined8 *param_1,char *param_2,undefined8 *param_3);
undefined8 * FUN_14000d664(undefined8 *param_1,undefined8 *param_2,char *param_3);
longlong * FUN_14000d6cc(longlong *param_1,longlong *param_2,char *param_3);
longlong * FUN_14000d744(longlong *param_1,longlong *param_2,undefined8 *param_3);
undefined8 * FUN_14000d7ac(undefined8 *param_1,undefined8 *param_2,undefined8 *param_3);
undefined8 * FUN_14000d954(undefined8 *param_1,undefined8 *param_2);
longlong * FUN_14000da44(longlong *param_1,char *param_2);
void FUN_14000dc38(longlong *param_1);
void FUN_14000dc9c(longlong *param_1);
void FUN_14000dd34(longlong *param_1,undefined8 *param_2);
void FUN_14000dff4(longlong *param_1);
void FUN_14000e0b4(int *param_1,undefined8 *param_2);
void FUN_14000e140(undefined8 *param_1);
void thunk_FUN_14000fce4(longlong *param_1);
void FUN_14000e16c(undefined8 *param_1,undefined1 param_2);
longlong * FUN_14000e2b0(longlong *param_1,undefined8 *param_2,ulonglong param_3);
longlong * FUN_14000e3d4(longlong *param_1,char *param_2);
longlong * FUN_14000e504(longlong *param_1,longlong *param_2);
longlong * FUN_14000e640(longlong *param_1,longlong *param_2);
undefined8 * FUN_14000e68c(undefined8 *param_1,char *param_2);
undefined8 * FUN_14000e6d8(longlong param_1,undefined8 *param_2);
void FUN_14000e830(longlong param_1);
longlong * FUN_14000e8ac(longlong *param_1);
ulonglong * FUN_14000e9a0(longlong param_1,ulonglong *param_2,longlong *param_3,uint param_4);
ulonglong *FUN_14000eaa4(longlong param_1,ulonglong *param_2,longlong param_3,int param_4,byte param_5);
ulonglong FUN_14000ec20(longlong param_1);
int FUN_14000ec7c(longlong param_1,int param_2);
int FUN_14000eccc(longlong param_1,int param_2);
void FUN_14000ee7c(undefined8 *param_1);
void _guard_check_icall(void);
undefined8 FUN_14000efd0(void);
undefined8 FUN_14000efd4(undefined8 param_1);
undefined8 * FUN_14000efd8(undefined8 param_1,undefined8 *param_2);
longlong FUN_14000eff4(longlong *param_1,undefined8 *param_2,ulonglong param_3);
longlong FUN_14000f088(longlong *param_1,undefined8 *param_2,ulonglong param_3);
int __thiscall std::basic_streambuf<>::uflow(basic_streambuf<> *this);
undefined8 FUN_14000f150(void);
void FUN_14000f154(undefined8 *param_1);
void * __thiscall std::basic_stringstream<>::`scalar_deleting_destructor'(basic_stringstream<> *this,uint param_1);
undefined8 * FUN_14000f1cc(undefined8 *param_1,uint param_2);
longlong * FID_conflict:`scalar_deleting_destructor'(undefined8 *param_1,uint param_2);
undefined8 * FUN_14000f264(undefined8 *param_1,ulonglong param_2);
undefined8 * FUN_14000f2e0(undefined8 *param_1,uint param_2);
char * FUN_14000f320(char *param_1,uint param_2);
undefined8 * FUN_14000f37c(undefined8 *param_1,undefined8 *param_2,undefined8 *param_3);
void FUN_14000f470(undefined8 *param_1,undefined8 *param_2,ulonglong param_3);
longlong * FUN_14000f530(longlong *param_1,undefined8 param_2,ulonglong param_3);
void FUN_14000f734(longlong *param_1,longlong *param_2,ulonglong param_3);
undefined8 *FUN_14000f8c4(undefined8 *param_1,undefined8 param_2,undefined8 param_3,undefined1 param_4);
longlong * FUN_14000f9e4(longlong *param_1,undefined8 *param_2,ulonglong param_3);
void FUN_14000fb5c(undefined8 *param_1,undefined8 *param_2,ulonglong param_3);
void FUN_14000fc10(longlong *param_1);
void ~sentry(longlong *param_1);
undefined8 * FUN_14000fc68(undefined8 *param_1,longlong *param_2);
void FUN_14000fce4(longlong *param_1);
void FUN_14000fd50(longlong *param_1);
undefined8 *FUN_14000fdb8(undefined8 *param_1,undefined8 param_2,undefined8 param_3,undefined8 *param_4,ulonglong param_5,undefined8 *param_6,ulonglong param_7);
longlong * FUN_14000fea0(longlong *param_1,undefined8 param_2,char param_3,int param_4);
void FUN_14000ff98(longlong *param_1);
undefined8 FUN_14000ffb4(byte *param_1);
void FUN_14000fffc(undefined8 *param_1);
void FUN_140010038(longlong param_1,char param_2);
longlong FUN_14001006c(longlong param_1);
TypeDescriptor * FUN_140010074(void);
void FUN_14001007c(longlong param_1,uint *param_2);
undefined8 * FUN_14001018c(longlong param_1);
void FUN_1400101f8(undefined8 param_1,char param_2);
TypeDescriptor * FUN_140010210(void);
undefined1 FUN_140010218(void);
undefined8 * FUN_14001021c(undefined8 param_1,undefined8 *param_2);
void FUN_14001022c(longlong *param_1);
void FUN_140010250(longlong *param_1,ulonglong param_2);
longlong * FUN_140010394(longlong *param_1);
void FUN_140010470(longlong *param_1);
undefined8 * FUN_1400104c0(undefined8 *param_1);
void FUN_14001057c(longlong param_1,undefined8 param_2,char param_3);
undefined8 * FUN_1400105e8(undefined8 *param_1,undefined8 *param_2,undefined8 *param_3);
void FUN_1400106a0(undefined8 *param_1);
void FUN_1400106e4(longlong param_1);
void FUN_140010774(void);
undefined1 FUN_140010788(longlong param_1,undefined1 param_2);
undefined8 * FUN_1400107f4(undefined8 *param_1,undefined8 *param_2);
void FUN_1400108ac(longlong *param_1);
void FUN_1400108d4(ulonglong param_1);
void FUN_140010910(longlong param_1,uint param_2);
void FUN_14001091c(longlong param_1,uint param_2);
ulonglong FUN_140010928(undefined1 *param_1,ulonglong param_2,longlong param_3,undefined8 param_4);
undefined8 * FUN_140010980(undefined8 *param_1,PUCHAR param_2,ulonglong param_3,PUCHAR param_4);
undefined8 * FUN_140010d1c(undefined8 *param_1,undefined8 param_2,undefined8 param_3);
longlong * FUN_140011284(longlong *param_1);
ulonglong * FUN_140011504(ulonglong *param_1);
void FUN_140011758(longlong *param_1,ulonglong param_2);
undefined8 * FUN_140011894(undefined8 *param_1,ulonglong param_2,undefined8 param_3);
longlong * FUN_140011940(longlong *param_1,undefined4 param_2);
void FUN_140011abc(undefined1 (*param_1) [32],byte param_2,ulonglong param_3);
longlong * FUN_140011b80(longlong param_1);
undefined8 *FUN_140011c90(undefined8 param_1,undefined8 *param_2,undefined8 *param_3,longlong param_4,byte param_5,undefined8 param_6);
undefined8 *FUN_140011d18(undefined8 param_1,undefined8 *param_2,undefined4 *param_3,longlong param_4,byte param_5,double param_6);
undefined8 *FUN_140011f3c(undefined8 param_1,undefined8 *param_2,undefined4 *param_3,longlong param_4,byte param_5,double param_6);
undefined8 *FUN_140012160(undefined8 param_1,undefined8 *param_2,undefined8 *param_3,longlong param_4,byte param_5,undefined8 param_6);
undefined8 *FUN_140012200(undefined8 param_1,undefined8 *param_2,undefined8 *param_3,longlong param_4,byte param_5,undefined8 param_6);
undefined8 *FUN_1400122a0(undefined8 param_1,undefined8 *param_2,undefined8 *param_3,longlong param_4,byte param_5,uint param_6);
undefined8 *FUN_140012340(undefined8 param_1,undefined8 *param_2,undefined8 *param_3,longlong param_4,byte param_5,uint param_6);
undefined8 *FUN_1400123e0(longlong *param_1,undefined8 *param_2,undefined8 *param_3,longlong param_4,byte param_5,char param_6);
undefined8 * FUN_1400125f4(undefined8 *param_1,ulonglong param_2);
undefined8 *FUN_140012620(undefined8 param_1,undefined8 *param_2,undefined8 *param_3,longlong param_4,byte param_5,byte *param_6,ulonglong param_7,char param_8);
longlong * FUN_1400129e4(longlong param_1);
undefined8 * FUN_140012af4(longlong param_1,undefined8 *param_2);
undefined8 * FUN_140012b3c(longlong param_1,undefined8 *param_2);
undefined8 * FUN_140012b84(longlong param_1,undefined8 *param_2);
undefined1 FUN_140012bcc(longlong param_1);
undefined1 FUN_140012bd0(longlong param_1);
undefined8 *FUN_140012bd4(undefined8 param_1,undefined8 *param_2,undefined8 *param_3,byte param_4,longlong param_5);
undefined8 *FUN_140012c5c(undefined8 param_1,undefined8 *param_2,undefined8 *param_3,byte *param_4,longlong param_5);
undefined8 *FUN_140012cec(undefined8 param_1,undefined8 *param_2,undefined8 *param_3,longlong param_4,byte param_5,char *param_6,ulonglong param_7);
undefined1 * FUN_140013028(undefined8 param_1,undefined1 *param_2,char *param_3,uint param_4);
undefined1 * FUN_1400130b8(undefined8 param_1,undefined1 *param_2,char param_3,uint param_4);
undefined8 FUN_140013164(longlong *param_1,longlong param_2);
void FUN_140013200(undefined8 *param_1);
undefined8 * FUN_14001326c(undefined8 *param_1,uint param_2);
undefined8 FUN_1400132cc(longlong *param_1,longlong param_2);
longlong * FUN_140013388(longlong *param_1,ulonglong param_2,ulonglong param_3,byte param_4);
longlong * FUN_140013554(longlong *param_1,ulonglong param_2,byte param_3);
void FUN_1400136d4(longlong param_1,undefined8 param_2,char param_3);
void FUN_140013790(void);
undefined8 * FUN_1400137a4(char *param_1);
void FUN_140013800(longlong *param_1);
undefined8 * FUN_140013830(undefined8 *param_1,uint param_2);
longlong * FUN_1400138b4(longlong *param_1,char *param_2);
undefined1 *FUN_140013af0(undefined1 *param_1,char *param_2,char *param_3,undefined8 *param_4,char *param_5,longlong *param_6);
undefined8 * FUN_140014900(undefined8 *param_1,undefined1 (*param_2) [32],undefined8 *param_3);
undefined8 * FUN_140014cbc(undefined8 *param_1,undefined1 (*param_2) [32],undefined8 *param_3);
undefined1 * FUN_140014e80(undefined1 *param_1,char *param_2,char *param_3);
undefined8 *FUN_140016434(undefined8 *param_1,undefined8 *param_2,longlong *param_3,char *param_4,longlong *param_5);
undefined1 * FUN_1400178cc(undefined1 *param_1,char *param_2,undefined8 param_3,char *param_4);
void FUN_140018668(char param_1,char param_2,undefined8 *param_3,longlong *param_4);
void FUN_140019aa8(char *param_1,char *param_2,undefined8 *param_3,char *param_4,char *param_5);
short * FUN_14001a938(short *param_1,char *param_2,char *param_3);
undefined8 * FUN_14001aa40(undefined8 *param_1,undefined8 *param_2,undefined8 *param_3);
undefined8 *FUN_14001ac14(undefined8 *param_1,undefined8 *param_2,longlong param_3,undefined8 param_4);
undefined8 *FUN_14001acbc(undefined8 *param_1,undefined8 *param_2,longlong param_3,undefined8 param_4);
undefined8 * FUN_14001ad64(undefined8 *param_1,longlong *param_2,undefined8 *param_3);
undefined8 * FUN_14001ae34(undefined8 *param_1,undefined1 (*param_2) [32]);
longlong * FUN_14001ae80(longlong *param_1,undefined8 param_2);
longlong * FUN_14001affc(longlong *param_1,undefined4 param_2);
longlong * FUN_14001b178(longlong *param_1,undefined4 param_2);
void FUN_14001b2f4(undefined8 *param_1,undefined8 *param_2,ulonglong param_3);
undefined8 * FUN_14001b3e4(undefined8 *param_1,longlong *param_2);
undefined8 * FUN_14001b490(undefined8 *param_1,undefined8 *param_2,ulonglong param_3);
undefined4 * FUN_14001b63c(undefined4 *param_1);
undefined8 * FUN_14001b654(undefined8 *param_1,undefined4 *param_2);
void FUN_14001b6ec(void);
char * FUN_14001b72c(void);
undefined8 * FUN_14001b734(undefined8 param_1,undefined8 *param_2,int param_3);
char * FUN_14001b788(void);
undefined8 * FUN_14001b790(undefined8 param_1,undefined8 *param_2,DWORD param_3);
int * FUN_14001b838(undefined8 param_1,int *param_2,int param_3);
undefined ** FUN_14001b890(void);
undefined4 * FUN_14001b898(undefined4 *param_1,undefined4 param_2);
void FUN_14001b8ac(undefined4 param_1);
undefined8 FUN_14001b8f0(void);
uint * FUN_14001b8f8(uint *param_1,uint *param_2);
uint * FUN_14001b9d4(uint *param_1,uint *param_2);
uint * FUN_14001bc38(uint *param_1,uint *param_2,uint *param_3);
undefined8 *FUN_14001bdec(undefined8 *param_1,undefined8 *param_2,undefined8 *param_3,undefined8 *param_4);
longlong * FUN_14001bf20(longlong param_1);
longlong *FUN_14001bf34(longlong *param_1,undefined8 *param_2,undefined8 *param_3,undefined8 *param_4);
undefined8 * FUN_14001c0d8(undefined8 *param_1,uint param_2);
void FUN_14001c138(undefined8 *param_1);
undefined8 * FUN_14001c174(undefined8 *param_1,longlong param_2);
void FUN_14001c26c(undefined8 param_1,undefined4 param_2,undefined8 *param_3);
void FUN_14001c2cc(char *param_1,undefined8 *param_2,undefined8 *param_3);
undefined8 FUN_14001c320(LPCWSTR param_1);
LPWSTR FUN_14001c3f8(LPWSTR param_1);
undefined1 FUN_14001c9c4(undefined8 *param_1,undefined8 *param_2,undefined8 param_3);
void FUN_14001ccf8(longlong param_1);
undefined1 FUN_14001cd28(ulonglong *param_1,undefined8 param_2,undefined8 param_3);
void FUN_14001d0c4(longlong param_1);
void FUN_14001d0f4(void);
longlong * FUN_14001d154(longlong *param_1,UINT param_2,undefined8 *param_3);
longlong * FUN_14001d260(longlong *param_1,wchar_t *param_2);
longlong * FUN_14001d3b4(longlong *param_1,wchar_t *param_2);
void FUN_14001d55c(undefined8 *param_1,undefined2 param_2);
void FUN_14001d6dc(undefined8 *param_1,ulonglong param_2);
void FUN_14001d858(longlong param_1);
void FUN_14001d8d8(longlong param_1);
void FUN_14001d958(longlong param_1,longlong param_2);
int FUN_14001d97c(longlong *param_1);
longlong FUN_14001d9c0(longlong param_1,longlong param_2,longlong param_3);
longlong * FUN_14001da14(longlong *param_1,longlong *param_2,longlong *param_3);
fpos_t * FUN_14001dad8(longlong *param_1,fpos_t *param_2,LARGE_INTEGER param_3,uint param_4);
longlong FUN_14001dbd0(longlong *param_1,undefined8 *param_2,ulonglong param_3);
longlong FUN_14001dc78(longlong *param_1,undefined8 *param_2,ulonglong param_3);
uint FUN_14001dd8c(longlong param_1);
int __thiscall std::basic_filebuf<>::underflow(basic_filebuf<> *this);
uint FUN_14001e000(longlong param_1,uint param_2);
int FUN_14001e0cc(longlong param_1,int param_2);
void FUN_14001e228(longlong param_1);
void FUN_14001e244(longlong param_1);
void FUN_14001e260(longlong *param_1);
longlong * FUN_14001e304(longlong *param_1,undefined8 param_2,longlong param_3);
undefined8 * FUN_14001e3ec(longlong *param_1,undefined8 *param_2);
longlong * FUN_14001e4bc(longlong *param_1);
longlong * FUN_14001e5d0(longlong *param_1,undefined8 param_2,longlong param_3);
void * __thiscall std::basic_stringstream<>::`scalar_deleting_destructor'(basic_stringstream<> *this,uint param_1);
void * __thiscall std::basic_stringstream<>::`scalar_deleting_destructor'(basic_stringstream<> *this,uint param_1);
longlong * FUN_14001e78c(longlong *param_1,uint param_2);
void FUN_14001e7c0(longlong *param_1);
longlong * FID_conflict:`scalar_deleting_destructor'(undefined8 *param_1,uint param_2);
undefined8 * FUN_14001e848(undefined8 *param_1,ulonglong param_2);
void FUN_14001e97c(undefined8 *param_1,undefined8 *param_2,ulonglong param_3);
longlong * FUN_14001ea4c(longlong param_1);
ulonglong FUN_14001eb5c(undefined8 param_1,undefined8 param_2,longlong param_3,longlong param_4,ulonglong param_5);
undefined8 FUN_14001eb78(undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4,undefined8 *param_5);
undefined8 FUN_14001eb88(undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4,undefined8 *param_5,undefined8 param_6,undefined8 param_7,undefined8 *param_8);
undefined1 FUN_14001ebb0(void);
undefined8 * FUN_14001ebb4(undefined8 *param_1,ulonglong param_2,undefined2 param_3);
void FUN_14001ed70(longlong param_1,longlong *param_2);
bool FUN_14001ede8(longlong *param_1);
void FUN_14001ee90(longlong param_1,longlong param_2,int param_3);
ulonglong FUN_14001ef88(longlong *param_1);
LPWSTR FUN_14001f00c(LPWSTR param_1,undefined8 *param_2);
longlong * FUN_14001f118(longlong *param_1,ulonglong param_2,undefined8 param_3,undefined8 *param_4);
undefined8 FUN_14001f23c(longlong *param_1,longlong param_2);
longlong FUN_14001f2d8(longlong param_1,wchar_t *param_2,uint param_3);
ulonglong FUN_14001f37c(longlong *param_1);
void FUN_14001f3d8(longlong param_1,uint param_2);
void FUN_14001f3e4(longlong param_1,uint param_2);
void FUN_14001f3f0(longlong param_1,uint param_2);
int FUN_14001f3fc(longlong param_1,uint param_2);
ulonglong FUN_14001f468(HANDLE param_1,longlong param_2,longlong param_3,longlong param_4,ulonglong param_5);
void FUN_14001f6e4(HANDLE param_1,char *param_2);
undefined8 FUN_14001f850(HANDLE param_1,longlong param_2,longlong param_3,longlong param_4);
undefined1 FUN_14001fb24(longlong *param_1,IMAGE_DOS_HEADER *param_2,longlong *param_3,longlong *param_4,int *param_5);
undefined8 * FUN_140022770(undefined8 *param_1,undefined8 *param_2,undefined8 *param_3);
longlong * FUN_140022968(longlong *param_1);
longlong * FUN_1400229a0(longlong *param_1,undefined2 param_2);
longlong * FUN_140022b1c(longlong *param_1,byte param_2);
undefined8 FUN_140022c2c(longlong *param_1);
ulonglong FUN_140023220(void);
ulonglong FUN_14002417c(longlong param_1,undefined8 *param_2,longlong *param_3);
void FUN_1400244b4(longlong param_1,longlong *param_2);
undefined8 FUN_140024c54(void);
void FUN_14002575c(longlong param_1,longlong *param_2);
undefined8 * FUN_140027140(undefined8 param_1,undefined8 *param_2);
undefined8 * FUN_1400271e4(undefined8 param_1,undefined8 *param_2);
undefined8 * FUN_140027280(undefined8 param_1,undefined8 *param_2);
undefined8 * FUN_140027324(undefined8 param_1,undefined8 *param_2);
undefined1 * FUN_1400273c0(undefined1 *param_1);
void FUN_1400284c8(longlong *param_1);
longlong * FUN_140028560(longlong *param_1,longlong *param_2,longlong *param_3);
void FUN_140028680(longlong *param_1);
undefined8 * FUN_140028738(undefined8 param_1,longlong *param_2,undefined8 *param_3);
void FUN_140028884(longlong *param_1,undefined8 *param_2);
undefined8 * FUN_140028b28(undefined8 param_1,longlong *param_2,undefined8 *param_3);
void FUN_140028c58(undefined8 *param_1);
void FUN_140028cf4(undefined8 *param_1);
void FUN_140028db4(undefined8 *param_1);
void FUN_140028e50(undefined8 *param_1);
void FUN_140028e80(longlong *param_1);
void FUN_140028eb0(undefined8 *param_1);
void FUN_140028ee0(void);
void FUN_140028ef4(longlong param_1);
undefined1 FUN_140028f74(undefined8 *param_1,ulonglong *param_2,short *param_3,longlong *param_4,char *param_5);
void FUN_140029b70(void);
void FUN_14002a988(longlong *param_1);
void FUN_14002a9a8(void);
void FUN_14002aa54(void);
void FUN_14002ab60(undefined8 *param_1,undefined8 *param_2);
undefined8 FUN_14002b438(undefined8 *param_1,undefined4 *param_2,undefined4 *param_3);
void FUN_14002b50c(undefined8 *param_1);
void FUN_14002b608(undefined8 *param_1);
void FUN_14002b704(longlong *param_1);
void FUN_14002b770(longlong *param_1);
void FUN_14002b7d8(longlong *param_1);
void FUN_14002b840(longlong param_1);
longlong * FUN_14002b914(longlong *param_1,longlong *param_2);
undefined8 *FUN_14002b9c0(undefined8 param_1,undefined8 *param_2,undefined8 *param_3,ulonglong param_4);
undefined8 *FUN_14002ba68(undefined8 param_1,undefined8 *param_2,undefined8 *param_3,ulonglong param_4);
longlong * FUN_14002bb10(longlong param_1,longlong *param_2,longlong *param_3);
undefined8 FUN_14002bc98(undefined8 *param_1);
void FUN_14002bcc0(longlong param_1);
void FUN_14002bcf4(longlong *param_1);
void FUN_14002bd10(ulonglong *param_1,ulonglong param_2,undefined8 param_3);
void FUN_14002be38(longlong param_1);
ulonglong FUN_14002be54(longlong param_1,ulonglong param_2);
void FUN_14002be88(longlong *param_1);
void FUN_14002c088(longlong *param_1);
void FUN_14002c288(void);
undefined8 *FUN_14002c29c(undefined8 *param_1,PUCHAR param_2,ULONG param_3,PUCHAR param_4,ULONG param_5);
longlong * FUN_14002c55c(longlong *param_1,longlong param_2,ulonglong param_3);
void FUN_14002c75c(void);
ulonglong FUN_14002c7b4(undefined8 *param_1,undefined8 *param_2,undefined8 *param_3);
undefined1 FUN_14002c9a0(undefined8 *param_1,PUCHAR param_2,LPCSTR param_3,LPCSTR param_4);
undefined8 * FUN_14002cfdc(undefined8 *param_1,undefined8 param_2,undefined8 param_3);
void FUN_14002d224(void);
longlong * FUN_14002d310(longlong *param_1);
ulonglong FUN_14002d494(LPCSTR param_1,longlong *param_2);
void FUN_14002d544(void);
DWORD FUN_14002da04(undefined8 *param_1,undefined1 param_2,undefined8 param_3);
undefined8 *FUN_14002de68(undefined8 *param_1,undefined8 *param_2,char *param_3,longlong param_4,longlong *param_5,undefined8 param_6);
undefined8 * FUN_14002f138(undefined8 param_1,undefined8 *param_2);
longlong * FUN_14002f23c(longlong *param_1,undefined1 param_2);
longlong * FUN_14002f3b8(longlong *param_1);
undefined8 * FUN_14002f444(undefined8 *param_1,byte *param_2,undefined8 param_3);
undefined8 * FUN_14002f504(undefined8 *param_1,byte *param_2,undefined8 param_3);
undefined1 FUN_14002fa00(WCHAR *param_1);
undefined8 FUN_14002fabc(WCHAR *param_1,longlong param_2,longlong param_3);
void FUN_14002fc10(int *param_1);
void FUN_14002fccc(int *param_1,uint param_2);
int __cdecl FID_conflict:printf_s(char *_Format,...);
void FUN_1400300d0(undefined1 (*param_1) [32],longlong param_2,undefined8 param_3,undefined8 param_4);
undefined1 (*) [32] FUN_140030120(undefined1 (*param_1) [32]);
void FUN_140030268(longlong param_1,undefined8 param_2,undefined8 *param_3);
void FUN_1400302d4(longlong param_1);
void thunk_FUN_14008be60(LPVOID param_1);
undefined4 * FUN_140030308(undefined4 *param_1);
void FUN_140030548(longlong param_1,float param_2);
void FUN_140030808(longlong param_1,int param_2);
void FUN_140030868(longlong param_1,ushort param_2);
void FUN_140030914(longlong param_1);
void FUN_140030970(longlong param_1,uint param_2,char param_3,float param_4);
void FUN_140030b10(longlong param_1,uint param_2,char param_3);
void FUN_140030b3c(longlong param_1,float param_2,float param_3);
void FUN_140030c50(longlong param_1,undefined8 param_2,longlong param_3);
void FUN_140030de0(longlong param_1,float param_2,float param_3);
void FUN_140030e5c(undefined1 *param_1,ulonglong param_2,longlong param_3,undefined8 param_4);
void FUN_140030ecc(undefined8 *param_1,longlong *param_2,char *param_3,longlong param_4);
void FUN_140030eec(undefined8 *param_1,longlong *param_2,char *param_3,longlong *param_4);
uint FUN_140031028(byte *param_1,longlong param_2,uint param_3);
uint FUN_14003105c(byte *param_1,longlong param_2,uint param_3);
undefined8 FUN_14003111c(LPCSTR param_1,LPCSTR param_2);
LPVOID FUN_1400312a8(LPCSTR param_1,undefined8 param_2,ulonglong *param_3);
int FUN_140031388(uint *param_1,byte *param_2,byte *param_3);
longlong FUN_140031500(byte *param_1,uint param_2);
uint FUN_1400315ac(float *param_1);
void FUN_140031650(float param_1,float param_2,float param_3,float *param_4,float *param_5,float *param_6);
void FUN_1400316f8(ulonglong param_1,float param_2,float param_3,float *param_4,float *param_5,float *param_6);
longlong FUN_140031844(longlong param_1,longlong param_2,uint param_3);
uint FUN_140031888(int *param_1,uint param_2,uint param_3);
void FUN_1400318d0(int *param_1,uint param_2,undefined8 param_3);
void FUN_140031938(int *param_1,char *param_2,longlong param_3);
void FUN_1400319f4(int *param_1,longlong param_2,undefined8 param_3,undefined8 param_4);
void FUN_140031a18(int *param_1,longlong param_2,undefined8 param_3);
void FUN_140031b44(int param_1,float param_2);
ulonglong FUN_140031b88(ulonglong param_1,float param_2);
void FUN_140031bc4(undefined8 param_1,uint param_2);
void FUN_140031d10(int param_1,undefined8 *param_2);
void FUN_140031e0c(int param_1);
void FUN_140031e84(int param_1,undefined4 param_2,float *param_3,ulonglong param_4);
void FUN_140031f04(int param_1,undefined4 param_2);
void FUN_140031f94(undefined8 param_1,undefined8 *param_2);
void FUN_140031ff4(int param_1);
char * FUN_1400320a0(char *param_1,char *param_2);
void FUN_1400320c8(undefined8 param_1,float *param_2,float *param_3,char param_4);
void FUN_1400321d0(float *param_1,float *param_2,float *param_3,float *param_4,undefined8 *param_5,float *param_6,float *param_7);
void FUN_140032440(int *param_1,float *param_2,float *param_3,ulonglong param_4,float *param_5,float *param_6,longlong *param_7);
void FUN_1400328d0(undefined8 param_1,undefined8 param_2,uint param_3,char param_4,float param_5);
void FUN_140032a68(undefined8 param_1,undefined8 param_2,float param_3);
void FUN_140032bac(float *param_1,int param_2,ulonglong param_3);
undefined8 * FUN_140032dcc(undefined8 param_1);
undefined8 * FUN_140032e34(undefined8 *param_1);
void FUN_140033d98(longlong param_1);
void FUN_140033db4(longlong param_1);
void FUN_140033dcc(longlong param_1);
void FUN_140033e00(longlong param_1);
void FUN_140033e18(longlong param_1);
void FUN_140033e54(longlong param_1);
void FUN_140033e90(longlong param_1,undefined8 param_2,undefined8 param_3);
void FUN_140034278(void);
void FUN_1400346a8(undefined8 param_1,char *param_2,float *param_3,ulonglong param_4);
void FUN_140034e44(longlong param_1,int param_2);
void FUN_140034e90(longlong param_1);
void FUN_140034ed8(longlong param_1,undefined8 param_2,undefined8 *param_3);
void FUN_14003505c(uint param_1,undefined8 *param_2,undefined8 param_3,undefined8 param_4);
void FUN_140035238(int param_1);
undefined8 FUN_14003527c(longlong param_1,byte param_2);
ulonglong FUN_1400352e8(uint param_1);
bool FUN_14003555c(float *param_1,int param_2,float *param_3,undefined8 param_4);
LPVOID FUN_1400357c8(ulonglong param_1);
void FUN_140035874(LPVOID param_1);
void FUN_1400358fc(int *param_1,int param_2,undefined8 param_3,longlong param_4);
void FUN_140035970(undefined8 param_1);
undefined1 (*) [32] FUN_140035994(longlong param_1,longlong param_2,undefined8 param_3);
undefined1 (*) [32] FUN_140035a88(longlong param_1);
void FUN_140035ce4(undefined8 *param_1,undefined8 param_2,undefined8 param_3,ulonglong param_4);
void FUN_140035d94(float *param_1,undefined8 param_2);
void FUN_140035ffc(void);
void FUN_14003620c(void);
void FUN_140037688(int *param_1,longlong param_2);
void FUN_140037708(longlong param_1,int param_2);
void FUN_1400377bc(float *param_1,float *param_2,char param_3);
void FUN_140037848(void);
void FUN_1400378c8(longlong param_1,uint param_2);
undefined8 *FUN_140037a48(undefined8 *param_1,byte *param_2,byte *param_3,char param_4,float param_5);
void FUN_140037b1c(float *param_1,undefined8 param_2,longlong *param_3,longlong *param_4);
void FUN_140037d1c(byte *param_1,float *param_2,ulonglong param_3,longlong param_4);
undefined1 FUN_140037d74(longlong param_1,uint param_2,float *param_3,ulonglong param_4,uint param_5);
void FUN_140038074(void);
void FUN_1400382b0(longlong param_1,byte param_2,char param_3);
undefined8 FUN_140038314(uint param_1);
void FUN_14003835c(longlong param_1,longlong param_2);
void FUN_1400383d4(longlong param_1,longlong param_2);
float * FUN_1400384d4(float *param_1,longlong param_2,float *param_3);
void FUN_140038690(longlong param_1,float *param_2,float *param_3);
float * FUN_1400387a4(float *param_1,longlong param_2,float *param_3,uint param_4);
void FUN_140038a8c(longlong param_1,float *param_2,float *param_3,float *param_4,undefined8 *param_5);
float * FUN_140038c50(float *param_1,longlong param_2,int param_3,float param_4,float param_5);
uint FUN_140038d44(longlong *param_1,float *param_2,float *param_3,float *param_4,uint *param_5,float *param_6);
void FUN_140039a7c(longlong param_1,int param_2,uint param_3,float param_4);
void FUN_140039d14(longlong param_1);
undefined1 FUN_140039d74(float *param_1,undefined8 param_2,uint param_3,longlong param_4);
void FUN_14003de18(void);
void FUN_14003e190(uint param_1,char param_2);
void FUN_14003e1d0(void);
void FUN_14003e210(char param_1);
void FUN_14003e26c(void);
void FUN_14003e2dc(undefined4 param_1);
void FUN_14003e3b4(void);
bool FUN_14003e3f8(uint param_1);
void FUN_14003e52c(longlong param_1,undefined8 *param_2,uint param_3);
void FUN_14003e670(undefined8 *param_1,int param_2,undefined8 *param_3);
void FUN_14003e6ac(undefined8 *param_1,undefined8 *param_2);
void FUN_14003e6e8(void);
void FUN_14003e740(int param_1);
void FUN_14003ea0c(longlong param_1,undefined4 param_2);
void FUN_14003eb88(void);
void FUN_14003ecf0(longlong param_1,float param_2);
void FUN_14003ee18(void);
uint FUN_14003ee70(longlong *param_1,byte *param_2,undefined8 param_3,longlong param_4);
uint FUN_14003eed0(longlong *param_1,int param_2,undefined8 param_3,longlong param_4);
void FUN_14003ef28(byte *param_1,undefined8 param_2,undefined8 param_3,longlong param_4);
void FUN_14003ef64(uint param_1,undefined8 param_2,undefined8 param_3,longlong param_4);
void FUN_14003efe4(void);
longlong FUN_14003f014(longlong param_1,uint param_2);
char * FUN_14003f070(uint param_1);
char * FUN_14003f0e8(uint param_1);
float * FUN_14003f21c(float *param_1,uint param_2,uint param_3,uint param_4,uint param_5);
void FUN_14003f388(longlong param_1);
uint FUN_14003f614(uint param_1);
bool FUN_14003f660(uint param_1,int param_2);
bool FUN_14003f6e0(uint param_1,byte param_2,int param_3);
ulonglong FUN_14003f96c(int param_1);
bool FUN_14003f9a0(int param_1,ulonglong param_2,int param_3);
ulonglong FUN_14003fa48(int param_1);
longlong FUN_14003fa7c(float *param_1,float *param_2,char param_3);
undefined8 FUN_14003fb20(undefined8 param_1,float param_2);
undefined8 FUN_14003fb4c(float *param_1);
void FUN_14003fb84(uint param_1,undefined1 param_2,undefined4 param_3);
uint FUN_14003fbf0(void);
void FUN_14003fc44(void);
void FUN_14003fe74(void);
void FUN_1400401f8(longlong param_1,float param_2,undefined8 param_3,undefined8 param_4);
longlong FUN_1400402a8(float *param_1);
void FUN_1400403dc(undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4);
char * FUN_140040874(uint param_1);
void FUN_1400408b8(undefined8 param_1,int *param_2,undefined8 param_3,undefined8 param_4);
void FUN_140040a74(char param_1,undefined8 param_2,ulonglong param_3,ulonglong param_4);
ulonglong FUN_140040f74(uint param_1,int param_2);
void FUN_14004104c(uint param_1,undefined4 param_2);
void FUN_1400410b4(uint param_1,undefined4 param_2);
undefined8 FUN_140041110(uint param_1,uint param_2,uint param_3);
void FUN_1400417b8(undefined2 *param_1);
void FUN_140041850(longlong param_1,char *param_2,float *param_3,ulonglong param_4);
undefined1 FUN_140041ab0(float *param_1,char *param_2,float *param_3,ulonglong param_4);
char FUN_140041c5c(void);
void FUN_140041eb4(int param_1);
undefined1 FUN_140041edc(float *param_1,float *param_2,float *param_3,longlong *param_4);
void FUN_14004258c(float *param_1,float param_2);
void FUN_1400426f0(float param_1,float param_2);
void FUN_140042794(float param_1);
void FUN_1400427f0(void);
void FUN_140042834(void);
undefined8 * FUN_140042898(undefined8 *param_1,undefined8 param_2,undefined4 param_3,float param_4);
undefined8 * FUN_14004296c(undefined8 *param_1);
void FUN_1400429b0(void);
void FUN_140042b60(void);
undefined8 * FUN_140042e30(undefined8 *param_1,longlong param_2);
float * FUN_140042f60(float *param_1,longlong param_2,float *param_3,uint param_4);
undefined8 FUN_140043414(ulonglong param_1);
void FUN_1400435a0(char *param_1,longlong param_2,undefined8 param_3,undefined8 param_4);
void FUN_1400435e8(char *param_1,longlong param_2,undefined8 param_3,undefined8 param_4);
ulonglong FUN_140043640(int param_1,uint param_2);
longlong FUN_1400436cc(void);
void FUN_140043708(undefined8 param_1,undefined8 param_2,ulonglong param_3,longlong param_4);
void FUN_140043760(uint param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4);
void FUN_140043950(longlong param_1,byte param_2);
void FUN_140043a28(uint param_1,byte param_2,ulonglong param_3,ulonglong param_4);
void FUN_140043bf4(void);
char FUN_140043cbc(uint param_1);
char FUN_140043d24(undefined8 param_1,undefined8 param_2,undefined8 param_3,longlong param_4);
void FUN_140043ddc(void);
char FUN_140043e90(undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4);
float * FUN_140043f30(float *param_1,float *param_2,float *param_3,uint *param_4,float *param_5,float *param_6,uint param_7);
float * FUN_14004420c(float *param_1);
float * FUN_1400442d4(float *param_1,longlong param_2);
ulonglong FUN_140044654(longlong param_1);
void FUN_140044690(ulonglong param_1,ulonglong param_2,undefined8 param_3,ulonglong param_4);
void FUN_140044b84(longlong param_1,undefined8 param_2,undefined8 param_3,ulonglong param_4);
void FUN_140044c00(void);
void FUN_140044c58(longlong param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4);
void FUN_140044cf4(undefined4 param_1,int param_2,int param_3,undefined8 *param_4);
void FUN_140044d98(int param_1,longlong param_2,undefined8 param_3,undefined8 param_4);
ulonglong FUN_140044ebc(longlong param_1,float *param_2);
void FUN_14004529c(longlong *param_1);
ulonglong FUN_140045308(void);
void FUN_140045330(undefined4 param_1,undefined4 param_2,uint param_3,undefined4 param_4);
void FUN_14004547c(longlong *param_1);
void FUN_1400454bc(int param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4);
void FUN_140045574(longlong param_1,char param_2);
float * FUN_140045664(float *param_1,uint param_2);
void FUN_1400458f4(undefined8 param_1,undefined8 param_2,ulonglong param_3,undefined8 *param_4);
void FUN_140046354(float *param_1,float *param_2,uint param_3,char param_4);
void FUN_140046410(undefined8 param_1,undefined8 param_2,ulonglong param_3,ulonglong param_4);
void FUN_140046b34(void);
void FUN_140046f34(void);
undefined8 FUN_140047110(void);
longlong FUN_1400473dc(uint param_1,uint param_2,int param_3);
void FUN_14004742c(int param_1);
void FUN_1400474b4(void);
void FUN_140047de8(undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4);
void FUN_140047eb0(longlong param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4);
void FUN_140047f58(longlong param_1,char *param_2,char *param_3,ulonglong param_4);
void FUN_14004810c(void);
void FUN_140048178(undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4);
void FUN_140048248(longlong param_1);
void FUN_140048270(undefined8 *param_1);
longlong FUN_140048358(byte *param_1);
void FUN_1400483a8(char *param_1,size_t param_2);
void FUN_1400485cc(LPCSTR param_1);
int * FUN_140048810(longlong param_1);
void FUN_14004887c(longlong param_1);
uint * FUN_1400488e4(undefined8 param_1,undefined8 param_2,byte *param_3);
void FUN_140048a1c(undefined8 param_1,undefined8 param_2,longlong param_3,undefined1 (*param_4) [32]);
ulonglong FUN_140048ae8(longlong param_1);
void FUN_140048be4(longlong param_1,undefined8 *param_2,int *param_3);
undefined8 FUN_140048f30(longlong param_1);
void FUN_1400490dc(undefined8 param_1,LPCSTR param_2);
bool FUN_1400491a8(undefined8 param_1,LPCSTR param_2);
void FUN_1400492f0(undefined8 param_1,longlong param_2,longlong param_3);
void FUN_1400493a0(longlong param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4);
void FUN_140049548(void);
void FUN_140049678(void);
void FUN_140049820(uint param_1,int param_2,char *param_3,longlong param_4);
void FUN_140049a8c(longlong param_1);
void FUN_140049aa4(undefined8 *param_1,undefined8 param_2,undefined8 param_3);
void FUN_140049bb8(undefined8 *param_1);
void FUN_140049c64(undefined8 *param_1,undefined8 param_2,undefined8 param_3);
void FUN_140049dcc(undefined8 *param_1);
void FUN_140049e98(undefined8 *param_1,undefined8 param_2,undefined8 param_3);
void FUN_14004a020(int *param_1,undefined8 *param_2);
void FUN_14004a0c0(int *param_1,int param_2);
void FUN_14004a124(int *param_1);
void FUN_14004a1a0(int *param_1,undefined8 *param_2);
void FUN_14004a238(int *param_1,undefined4 *param_2);
void FUN_14004a2c0(int *param_1,undefined8 *param_2);
void FUN_14004a34c(int *param_1,int param_2);
longlong FUN_14004a3c4(int *param_1,longlong param_2,undefined8 *param_3);
void FUN_14004a490(int *param_1,int param_2,undefined1 (*param_3) [32]);
void FUN_14004a5b4(int *param_1,int param_2);
void FUN_14004a624(int *param_1,undefined2 *param_2);
void FUN_14004a6ac(int *param_1,int param_2);
longlong FUN_14004a720(int *param_1,longlong param_2,undefined8 *param_3);
void FUN_14004a830(int *param_1,undefined8 *param_2);
int FUN_14004a8ec(undefined8 param_1,int *param_2,int param_3,int param_4,int *param_5);
uint FUN_14004a978(longlong *param_1,int param_2);
longlong * FUN_14004a9b4(longlong *param_1,longlong *param_2);
void FUN_14004aa7c(longlong *param_1,uint param_2,int param_3,longlong param_4);
longlong * FUN_14004acfc(longlong *param_1,longlong *param_2,int param_3);
uint FUN_14004adc0(longlong param_1,uint param_2,char *param_3);
longlong * FUN_14004ae70(longlong *param_1,longlong *param_2,longlong *param_3);
uint FUN_14004af44(longlong param_1,uint param_2);
int FUN_14004b2b8(longlong param_1,int param_2);
int FUN_14004b388(longlong param_1,int param_2,int param_3,int param_4,undefined2 param_5,undefined2 param_6,int param_7,int param_8,int param_9,int param_10);
int FUN_14004b460(longlong param_1,int param_2,undefined8 *param_3);
void FUN_14004bd10(longlong param_1,int param_2,int param_3);
void FUN_14004bd54(int *param_1,char param_2,int param_3,int param_4,int param_5,int param_6,int param_7,int param_8);
void FUN_14004bde4(int *param_1,float param_2,float param_3);
void FUN_14004be94(int *param_1,float param_2,float param_3);
void FUN_14004bed4(int *param_1,float param_2,float param_3,float param_4,float param_5,float param_6,float param_7);
undefined8 FUN_14004bf68(longlong param_1,int param_2,int *param_3);
undefined4 FUN_14004cdec(longlong param_1,int param_2,undefined8 *param_3);
void FUN_14004ce98(longlong param_1,int param_2,float param_3,float param_4,float param_5,float param_6,int *param_7,int *param_8,int *param_9,int *param_10);
void FUN_14004d18c(longlong param_1,int param_2,longlong param_3,float param_4,float param_5,float param_6,float param_7);
void FUN_14004d2b4(undefined8 *param_1,uint param_2);
undefined8 FUN_14004d410(longlong param_1,int *param_2,float param_3,float param_4,float param_5,float param_6,float param_7,float param_8,float param_9,int param_10);
void FUN_14004d5f8(longlong param_1,int *param_2,float param_3,float param_4,float param_5,float param_6,float param_7,float param_8,float param_9,float param_10,float param_11,int param_12);
void FUN_14004d9f4(longlong param_1);
void FUN_14004e130(longlong param_1);
undefined1 (*) [32] FUN_14004e160(undefined1 (*param_1) [32],longlong param_2);
void FUN_14004e288(undefined8 *param_1,undefined8 param_2,undefined8 *param_3);
void FUN_14004e70c(longlong param_1,longlong param_2);
void FUN_14004e7e8(uint *param_1);
void FUN_14004ea10(undefined8 *param_1);
void FUN_14004eaf8(int *param_1);
void FUN_14004ebfc(uint *param_1);
void FUN_14004ec24(int *param_1);
uint FUN_14004eccc(longlong param_1,float param_2);
void FUN_14004ed54(int *param_1,float *param_2,float *param_3,char param_4);
void FUN_14004ef34(int *param_1);
void FUN_14004f004(int *param_1,undefined8 *param_2);
void FUN_14004f14c(int *param_1);
void FUN_14004f240(int *param_1,int param_2,int param_3);
void FUN_14004f2ec(longlong param_1,undefined8 *param_2,undefined8 *param_3,undefined8 *param_4,undefined8 *param_5,undefined4 param_6);
void FUN_14004f468(int *param_1,float *param_2,uint param_3,uint param_4,byte param_5,float param_6);
void FUN_14005019c(longlong param_1,float *param_2,float param_3,uint param_4,int param_5);
void FUN_14005042c(longlong param_1,float *param_2,float param_3,int param_4,int param_5);
void FUN_140050464(longlong param_1,float *param_2,float param_3,undefined8 param_4,float param_5,int param_6);
void FUN_1400508fc(longlong param_1,float *param_2,float *param_3,float param_4,uint param_5);
void FUN_140050b90(int *param_1,undefined8 *param_2,undefined8 *param_3,uint param_4,float param_5);
void FUN_140050c28(int *param_1,float *param_2,float *param_3,uint param_4,float param_5,undefined8 param_6,float param_7);
void FUN_140050d18(int *param_1,float *param_2,float *param_3,uint param_4,float param_5,uint param_6);
void FUN_140050ebc(int *param_1,undefined8 *param_2,undefined8 *param_3,undefined8 *param_4,uint param_5);
void FUN_140050f10(int *param_1,float *param_2,float param_3,uint param_4,int param_5,float param_6);
void FUN_140050fec(int *param_1,float *param_2,float param_3,uint param_4,int param_5);
void FUN_14005109c(int *param_1,longlong *param_2,float param_3,float *param_4,float param_5,float *param_6,float *param_7,float param_8,float *param_9);
void FUN_14005119c(int *param_1,float *param_2,float param_3,float *param_4);
void FUN_1400511e4(int *param_1,longlong *param_2,undefined8 *param_3,undefined8 *param_4,undefined8 *param_5,undefined8 *param_6,uint param_7);
void FUN_1400513cc(int *param_1,longlong *param_2,float *param_3,float *param_4,float *param_5,float *param_6,uint param_7,float param_8);
void FUN_1400515c8(int *param_1);
void FUN_140051684(int *param_1,uint *param_2);
void FUN_1400519c8(int *param_1,int *param_2,int param_3);
void FUN_140051a9c(longlong param_1,int *param_2,int *param_3);
undefined1 (*) [32] FUN_140051ba8(undefined1 (*param_1) [32]);
undefined1 (*) [32] FUN_140051be8(undefined1 (*param_1) [32]);
void FUN_140051cbc(longlong param_1,undefined8 *param_2,undefined8 param_3,longlong param_4);
void FUN_140051e40(uint *param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4);
void FUN_140051ea0(longlong param_1,undefined8 *param_2,undefined8 param_3,longlong param_4);
void FUN_140051fcc(longlong param_1,undefined8 *param_2,undefined8 param_3,longlong param_4);
void FUN_14005214c(uint *param_1,char *param_2,float *param_3,ulonglong param_4);
void FUN_140052430(longlong param_1,int param_2,int param_3,longlong param_4,int param_5,int param_6,int param_7,int param_8);
void FUN_1400524bc(longlong param_1,longlong param_2,ushort param_3,ushort param_4,ushort param_5,ushort param_6);
undefined1 (*) [32]FUN_140052674(uint *param_1,undefined8 *param_2,undefined8 param_3,longlong param_4);
void FUN_1400529f8(uint *param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4);
void FUN_140052a40(uint *param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4);
void FUN_140052b5c(uint *param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4);
undefined1 (*) [32]FUN_140052c88(uint *param_1,FILE *param_2,float param_3,undefined4 *param_4,undefined8 param_5);
void FUN_1400533d8(uint *param_1,undefined8 param_2,undefined8 param_3,float param_4,undefined4 *param_5,longlong param_6);
void FUN_140053550(uint *param_1,undefined1 *param_2,undefined8 param_3,float param_4,undefined4 *param_5);
void FUN_1400539a0(longlong param_1,longlong param_2,longlong param_3);
uint FUN_140053a9c(uint *param_1,uint param_2,uint param_3,undefined2 *param_4);
ulonglong FUN_140053b4c(longlong param_1,ulonglong param_2,undefined2 *param_3);
void FUN_140053c50(uint *param_1,undefined8 *param_2,undefined *param_3,ulonglong param_4);
void FUN_14005416c(longlong param_1);
void FUN_1400542ac(longlong param_1,int param_2,int param_3,int param_4,int param_5,longlong param_6,char param_7);
void FUN_140054380(uint *param_1);
void FUN_1400544c0(uint *param_1);
undefined8 FUN_140054778(longlong param_1,longlong param_2);
void FUN_1400547e0(longlong param_1,undefined8 *param_2,undefined8 param_3,longlong param_4);
undefined8 FUN_140054864(longlong param_1,undefined8 *param_2,longlong param_3,longlong param_4);
void FUN_140054a48(undefined8 param_1,longlong param_2);
void FUN_140054a84(longlong param_1,undefined8 *param_2,undefined8 *param_3,undefined8 param_4);
void FUN_140054c08(longlong param_1,undefined8 *param_2,undefined8 param_3,longlong param_4);
void FUN_140054f30(longlong param_1,int param_2,undefined8 param_3,undefined8 param_4);
void FUN_140054fc4(longlong param_1,longlong param_2);
undefined1 (*) [32] FUN_1400550e4(longlong param_1,int param_2,int param_3);
void FUN_14005536c(uint *param_1,int param_2,int param_3);
void FUN_140055a74(uint *param_1,uint param_2,uint param_3);
int * FUN_140055b78(int *param_1,uint *param_2);
void FUN_140055d90(uint *param_1,undefined8 param_2,undefined8 param_3,longlong param_4);
void FUN_1400562c8(longlong param_1,undefined8 param_2,undefined8 param_3,longlong param_4);
void FUN_140056360(longlong param_1,ulonglong param_2);
uint FUN_140056434(uint *param_1,uint param_2,uint param_3,uint *param_4);
void FUN_140056a54(int *param_1,int param_2);
uint * FUN_140056c80(int *param_1,ushort param_2,float *param_3);
uint FUN_140057294(int *param_1,ushort param_2);
longlong FUN_1400572f4(undefined8 param_1,longlong param_2,undefined8 param_3,int *param_4);
void FUN_140057984(undefined8 param_1,longlong param_2);
bool FUN_1400579b0(undefined8 param_1,longlong param_2,ushort param_3);
undefined8 FUN_1400579d0(undefined8 param_1,longlong param_2,longlong param_3);
undefined8 FUN_140057a94(uint *param_1,longlong param_2,longlong param_3,undefined8 param_4,uint param_5,uint *param_6,float *param_7);
void FUN_140059478(undefined8 *param_1,undefined8 param_2,undefined8 param_3,longlong param_4);
uint * FUN_1400594c0(longlong param_1,int *param_2,longlong param_3,undefined8 *param_4);
uint * FUN_140059760(int *param_1,ushort param_2);
uint * FUN_1400597d0(int *param_1,ushort param_2);
uint * FUN_14005982c(longlong *param_1,float param_2,float param_3,ulonglong param_4);
void FUN_140059e1c(longlong param_1,uint param_2,uint param_3);
void FUN_140059e7c(undefined8 param_1,undefined8 param_2,uint param_3,int param_4,byte *param_5);
byte * FUN_140059f08(longlong *param_1,float param_2,byte *param_3,byte *param_4,float param_5,uint param_6);
float * FUN_14005a17c(float *param_1,longlong *param_2,float param_3,float param_4,float param_5,byte *param_6,byte *param_7,byte *param_8,longlong *param_9,undefined8 param_10,uint param_11);
void FUN_14005a430(longlong *param_1,int *param_2,float param_3,float *param_4,uint param_5,ushort param_6,float *param_7);
void FUN_14005a6ec(longlong *param_1,int *param_2,float param_3,float *param_4,float param_5,float *param_6,float *param_7,float *param_8,float param_9,uint param_10);
void FUN_14005ae30(int *param_1,undefined8 param_2,uint param_3,int param_4,float param_5);
void FUN_14005afd8(int *param_1,undefined8 param_2,uint param_3);
void FUN_14005b030(int *param_1,undefined8 param_2,uint param_3,float param_4);
void FUN_14005b19c(undefined1 *param_1,uint param_2);
void FUN_14005b1f0(undefined8 *param_1,uint param_2);
int * FUN_14005b238(int *param_1,int *param_2);
void FUN_14005b328(int *param_1,int param_2);
void FUN_14005b3a8(int *param_1,int param_2);
void FUN_14005b428(longlong param_1);
int FUN_14005b4dc(longlong param_1,int param_2,int param_3);
void FUN_14005b584(longlong param_1,int param_2,int param_3);
void FUN_14005b644(uint *param_1);
void FUN_14005d704(undefined8 param_1,char *param_2,float *param_3,ulonglong param_4);
void FUN_14005edfc(longlong param_1,undefined8 param_2,undefined8 param_3,ulonglong param_4);
void FUN_14005f49c(longlong param_1);
uint FUN_14005f5a8(longlong param_1,int param_2,int param_3);
void FUN_14005f640(undefined8 *param_1,undefined4 param_2,undefined2 param_3,int param_4);
void FUN_14005f6b0(longlong param_1);
int * FUN_14005f780(undefined8 param_1,undefined8 param_2,undefined1 (*param_3) [32]);
void FUN_14005f948(undefined8 param_1,undefined8 param_2,longlong param_3,undefined1 (*param_4) [32]);
void FUN_14005fbb0(longlong param_1,undefined8 *param_2,int *param_3);
void FUN_14005fe48(void);
float FUN_140060060(int param_1);
void FUN_1400600ac(int param_1,float param_2);
void FUN_1400601a4(void);
void FUN_140060598(float *param_1,float *param_2);
void FUN_140060a40(char *param_1,longlong param_2,undefined8 param_3,undefined8 param_4);
void FUN_140060a68(char *param_1,longlong *param_2);
void FUN_140060abc(undefined8 *param_1,char *param_2,longlong param_3,undefined8 param_4);
void FUN_140060b00(char *param_1,longlong param_2,undefined8 param_3,undefined8 param_4);
void FUN_140060b68(char *param_1,longlong param_2,undefined8 param_3,undefined8 param_4);
bool FUN_140060d6c(float *param_1,uint param_2,char *param_3,undefined1 *param_4,uint param_5);
bool FUN_140061528(float *param_1,undefined8 *param_2,undefined8 param_3,longlong param_4);
void FUN_1400617e8(float *param_1,undefined8 param_2,undefined8 param_3,longlong param_4);
bool FUN_140061838(byte *param_1,float *param_2,undefined4 param_3);
undefined1 FUN_140061964(byte *param_1,int param_2,undefined8 param_3,longlong param_4);
void FUN_140061bbc(uint param_1,undefined8 param_2,undefined8 param_3,longlong param_4);
char FUN_140061e30(float *param_1,float *param_2,uint param_3,longlong *param_4,longlong param_5,longlong param_6,uint param_7);
undefined1 FUN_1400624e4(undefined8 param_1,undefined8 param_2,undefined8 param_3,longlong param_4);
undefined8 FUN_140062b9c(undefined8 param_1,undefined8 param_2,float *param_3,longlong param_4);
void FUN_140062fec(float *param_1);
void FUN_14006306c(void);
void FUN_140063458(undefined8 param_1,float *param_2,float *param_3,float param_4);
void FUN_140063850(float *param_1);
void FUN_1400638c4(undefined1 *param_1,uint param_2,float param_3,float param_4);
float FUN_140063a80(int param_1);
ulonglong FUN_140063ac8(undefined8 param_1,undefined8 param_2,undefined8 param_3,longlong param_4);
void FUN_1400640bc(void);
void FUN_140064120(float *param_1,float *param_2,float *param_3,undefined8 param_4,float param_5);
float FUN_140064164(longlong *param_1,int param_2,int param_3);
void FUN_140064238(undefined4 *param_1,longlong *param_2,int param_3);
int FUN_1400642fc(longlong param_1,int param_2);
undefined8 FUN_140064334(longlong param_1,int param_2);
ulonglong FUN_140064450(longlong param_1,int param_2);
int FUN_1400644cc(longlong param_1,int param_2);
int FUN_140064610(longlong *param_1,int param_2);
ulonglong FUN_140064674(longlong *param_1,longlong param_2,int param_3);
int FUN_140064794(longlong *param_1,longlong param_2,int param_3);
int FUN_14006487c(longlong param_1,uint param_2,byte *param_3,int param_4);
void FUN_1400649ac(float *param_1,longlong *param_2,float param_3,int param_4);
void FUN_140064b04(longlong param_1,int *param_2);
void FUN_140064b38(longlong param_1,longlong param_2,int param_3,uint param_4);
void FUN_140064c08(longlong param_1,int *param_2);
void FUN_140064db4(longlong *param_1,float *param_2,uint param_3);
longlong FUN_140065824(undefined8 *param_1,undefined4 param_2,int param_3,undefined4 param_4);
void FUN_140065a28(longlong param_1,int *param_2);
void FUN_140065c68(longlong param_1,int *param_2);
void FUN_140065dac(longlong param_1);
void FUN_140065df8(longlong *param_1,uint param_2);
void FUN_140065e64(longlong param_1,uint param_2);
bool FUN_140065f60(longlong param_1,longlong param_2,uint *param_3,undefined8 param_4,undefined8 param_5,char param_6);
ulonglong FUN_1400661c4(float *param_1,float *param_2,float *param_3,longlong param_4,undefined8 *param_5,float param_6,ulonglong param_7,undefined8 param_8);
void FUN_14006a500(void);
char FUN_14006a994(float *param_1,char param_2,ulonglong param_3,float *param_4);
int * FUN_14006b41c(undefined8 param_1,char *param_2,float *param_3,ulonglong param_4);
void FUN_14006bd44(int param_1,char *param_2,char *param_3,undefined8 param_4);
void FUN_14006c51c(int *param_1);
float FUN_14006c5e0(uint *param_1,float param_2,float param_3,float param_4,float param_5);
void FUN_14006c678(undefined8 param_1,undefined8 param_2,undefined8 param_3,ulonglong param_4);
ulonglong FUN_14006c8d4(void);
char FUN_14006c960(float *param_1,undefined8 param_2,float *param_3,char param_4,char param_5);
void FUN_14006ce1c(undefined8 param_1,char *param_2,uint *param_3);
void FUN_14006cf54(longlong param_1,undefined8 param_2,uint *param_3);
float * FUN_14006e098(float *param_1,byte *param_2,char param_3);
void FUN_14006e154(int *param_1);
ulonglong FUN_14006e1d8(undefined8 param_1);
void FUN_14006e360(void);
undefined1 FUN_14006e40c(byte *param_1,int param_2);
undefined1 FUN_14006e4d0(void);
void FUN_14006e508(longlong param_1,longlong *param_2);
void FUN_14006e7c8(longlong param_1);
void FUN_14006f134(longlong param_1);
void FUN_14006f19c(longlong param_1);
void FUN_14006f364(void);
undefined8 FUN_14006f4a0(longlong *param_1,undefined1 (*param_2) [32]);
void FUN_14006f5e4(void);
undefined8 FUN_14006f680(undefined8 param_1);
int * FUN_14006f6e0(int *param_1,int param_2);
void FUN_14006f730(void);
void FUN_14006f780(int *param_1);
undefined8 *FUN_14006f7b0(undefined8 *param_1,undefined4 param_2,undefined8 param_3,undefined8 *param_4);
void FUN_14006f8f0(int param_1);
undefined8 * FUN_14006f940(undefined8 *param_1);
undefined8 * FUN_14006f970(undefined8 *param_1,longlong param_2);
undefined8 * FUN_14006f9b0(undefined8 *param_1,longlong param_2);
undefined8 * FUN_14006fa00(undefined8 *param_1,longlong param_2);
undefined8 * FUN_14006fa40(undefined8 *param_1,longlong param_2);
undefined8 * FUN_14006fa90(undefined8 *param_1,longlong param_2);
undefined8 * FUN_14006fad0(undefined8 *param_1,longlong param_2);
undefined8 * FUN_14006fb10(undefined8 *param_1,longlong param_2);
undefined8 * FUN_14006fb60(undefined8 *param_1,longlong param_2);
void FUN_14006fbb0(void);
void FUN_14006fbd0(longlong param_1);
void FUN_14006fc00(longlong param_1);
void FUN_14006fc30(longlong param_1);
void FUN_14006fc60(longlong param_1);
undefined8 * FUN_14006fc90(undefined8 *param_1);
undefined8 * FUN_14006fd00(undefined8 *param_1,uint param_2);
void FUN_14006fdc0(undefined8 param_1);
undefined8 FUN_14006fe00(void);
longlong * FUN_14006fe10(char param_1);
void FUN_14006ff00(longlong param_1,char *param_2);
void FUN_14006ffc0(longlong param_1);
undefined8 * FUN_14006ffe0(undefined1 param_1);
void FUN_140070060(void);
bool thunk_FUN_140075a30(void);
void FUN_1400700d0(longlong param_1);
void FUN_140070150(longlong param_1);
char * FUN_140070250(int param_1);
int FUN_140070280(int param_1);
undefined4 * FUN_1400702b0(undefined4 *param_1);
ulonglong FUN_140070390(uint param_1,UINT *param_2);
undefined8 FUN_1400704d0(uint *param_1);
undefined8 FUN_140070560(longlong param_1);
void FUN_140070590(void);
ulonglong FUN_140070680(uint param_1,UINT *param_2);
undefined4 FUN_1400707c0(undefined8 *param_1);
DWORD __stdcall GetCurrentThreadId(void);
undefined4 FUN_1400707f0(undefined8 *param_1,DWORD *param_2);
undefined8 * FUN_140070870(undefined8 *param_1,undefined8 param_2);
undefined8 * FUN_1400708a0(undefined8 *param_1);
ulonglong FUN_1400709b0(undefined1 (*param_1) [16],ulonglong param_2,undefined1 (*param_3) [16],ulonglong param_4);
ulonglong FUN_140070b20(undefined1 (*param_1) [16],ulonglong param_2,undefined1 (*param_3) [16],ulonglong param_4);
undefined1 (*) [32]FUN_140070c90(undefined1 (*param_1) [32],undefined1 (*param_2) [32],undefined1 (*param_3) [32],ulonglong param_4);
undefined1 (*) [32]FUN_140071030(undefined1 (*param_1) [32],undefined1 (*param_2) [32],undefined1 (*param_3) [32],ulonglong param_4);
undefined1 (*) [32]FUN_1400713f0(undefined1 (*param_1) [32],undefined1 (*param_2) [32],byte param_3);
undefined1 (*) [32]FUN_1400714f0(undefined1 (*param_1) [32],undefined1 (*param_2) [32],byte param_3);
undefined1 *FUN_140071610(undefined1 (*param_1) [16],ulonglong param_2,undefined1 (*param_3) [16],ulonglong param_4);
longlong FUN_140071830(longlong param_1,ulonglong param_2,byte *param_3,ulonglong param_4);
longlong FUN_140071a60(longlong param_1,ulonglong param_2,byte *param_3,ulonglong param_4);
ulonglong FUN_140071c90(undefined8 *param_1,ulonglong param_2,byte *param_3,ulonglong param_4);
undefined1 (*) [16]FUN_140071ec0(undefined1 (*param_1) [16],ulonglong param_2,undefined1 (*param_3) [16],ulonglong param_4);
undefined1 (*) [16]FUN_1400720e0(undefined1 (*param_1) [16],ulonglong param_2,undefined1 (*param_3) [16],ulonglong param_4);
ulonglong FUN_1400722c0(longlong param_1,longlong param_2,ulonglong param_3);
char FUN_1400723c0(ulonglong param_1,ulonglong param_2,char param_3);
undefined1 (*) [32]FUN_140072450(undefined1 (*param_1) [32],undefined1 (*param_2) [32],undefined1 (*param_3) [16],ulonglong param_4);
ulonglong FUN_140072640(undefined8 *param_1,uint param_2);
undefined1 (*) [32]thunk_FUN_140071030(undefined1 (*param_1) [32],undefined1 (*param_2) [32],undefined1 (*param_3) [32],ulonglong param_4);
ulonglong thunk_FUN_1400709b0(undefined1 (*param_1) [16],ulonglong param_2,undefined1 (*param_3) [16],ulonglong param_4);
ulonglong thunk_FUN_140070b20(undefined1 (*param_1) [16],ulonglong param_2,undefined1 (*param_3) [16],ulonglong param_4);
undefined1 *FUN_140072700(undefined1 (*param_1) [16],undefined1 *param_2,undefined1 (*param_3) [16],ulonglong param_4);
undefined1 (*) [32]thunk_FUN_1400713f0(undefined1 (*param_1) [32],undefined1 (*param_2) [32],byte param_3);
ulonglong thunk_FUN_1400722c0(longlong param_1,longlong param_2,ulonglong param_3);
undefined1 (*) [32]thunk_FUN_140072450(undefined1 (*param_1) [32],undefined1 (*param_2) [32],undefined1 (*param_3) [16],ulonglong param_4);
void FUN_1400728a0(DWORD param_1,longlong *param_2);
HLOCAL __stdcall LocalFree(HLOCAL hMem);
DWORD FUN_140072990(HANDLE param_1);
uint FUN_140072a10(void);
ulonglong FUN_140072a40(UINT param_1,LPCSTR param_2,int param_3,LPWSTR param_4,int param_5);
undefined8 FUN_140072a90(UINT param_1,LPCWSTR param_2,int param_3,LPSTR param_4,int param_5);
ulonglong FUN_140072b40(LPCWSTR param_1);
DWORD FUN_140072bc0(LPCWSTR param_1,ulonglong *param_2,uint param_3,uint param_4);
DWORD FUN_140072f00(longlong *param_1,undefined8 param_2,undefined4 param_3,uint param_4);
undefined8 FUN_140072f80(undefined8 param_1);
_iobuf * FUN_1400732b0(wchar_t *param_1,uint param_2,int param_3);
undefined8 FUN_140073370(longlong param_1);
undefined8 FUN_140073390(longlong param_1);
undefined8 FUN_1400733b0(longlong param_1,longlong param_2);
LARGE_INTEGER FUN_140073400(void);
void FUN_140073420(void);
void __stdcall Sleep(DWORD dwMilliseconds);
void __stdcall DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
void FUN_140073470(LPCRITICAL_SECTION param_1);
void __stdcall EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
void __stdcall LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
void FUN_1400734a0(PVOID param_1);
ulonglong FUN_1400734e0(LPCWSTR param_1,uint param_2,char *param_3,int param_4,undefined8 param_5,int param_6,UINT param_7,int param_8);
void FUN_1400737e0(void);
void FUN_1400737f0(undefined4 *param_1);
void _Init_thread_footer(int *param_1);
void FUN_1400738a0(int *param_1);
void FUN_140073940(ulonglong param_1);
ulonglong FUN_140073990(void);
ulonglong FUN_1400739e0(int param_1);
ulonglong FUN_140073a20(uint param_1);
ulonglong FUN_140073ac0(longlong param_1);
undefined8 FUN_140073b60(char param_1);
undefined8 FUN_140073b90(bool param_1,char param_2);
int FUN_140073bc0(longlong param_1);
undefined8 * FUN_140073c00(undefined8 *param_1,ulonglong param_2);
void thunk_FUN_140073940(ulonglong param_1);
void FUN_140073c40(longlong param_1,longlong param_2,longlong param_3,undefined *param_4);
void FUN_140073cc0(longlong param_1,longlong param_2,longlong param_3,undefined *param_4);
void FUN_140073d20(void);
undefined8 FUN_140073de0(void);
void FUN_140073df0(void);
ulonglong FUN_140073e10(void);
void entry(void);
undefined8 FUN_140073fb0(int *param_1,longlong param_2,ULONG_PTR param_3,ulonglong *param_4);
void __cdecl __security_check_cookie(uintptr_t _StackCookie);
undefined8 FUN_140074050(void);
longlong FUN_1400740d0(longlong param_1,longlong param_2);
undefined8 FUN_140074140(void);
void __chkstk(void);
bool FUN_140074450(void);
void FUN_140074460(void);
void FUN_140074470(undefined4 param_1);
ulonglong FUN_140074480(void);
undefined8 thunk_FUN_14000efd0(void);
bool FUN_1400744f0(void);
void FUN_140074550(void);
undefined8 FUN_140074560(undefined8 *param_1);
void FUN_1400745c0(void);
undefined8 FUN_140074680(void);
void FUN_140074690(void);
void FUN_1400746a0(void);
bool FUN_1400746c0(void);
undefined * FUN_1400746d0(void);
undefined * FUN_1400746e0(void);
void FUN_1400746f0(void);
void FUN_140074740(void);
void FUN_140074790(void);
void FUN_1400747a0(void);
undefined4 FUN_1400747b0(undefined4 *param_1,undefined8 param_2,undefined8 param_3);
undefined4 FUN_140074800(undefined4 *param_1,undefined8 param_2,undefined8 param_3);
longlong FUN_140074850(byte *param_1,byte *param_2,longlong param_3,int param_4,char param_5);
bool FUN_140074960(ulonglong *param_1,longlong param_2);
byte FUN_1400749d0(undefined8 param_1,byte *param_2);
void FUN_1400749e0(longlong *param_1,ulonglong *param_2,longlong param_3);
void FUN_140074b10(longlong *param_1,ulonglong *param_2,byte *param_3);
longlong * FUN_140074b50(longlong *param_1,ulonglong *param_2,longlong param_3,longlong *param_4);
longlong * FUN_140074c50(longlong *param_1,undefined8 param_2,byte *param_3,longlong *param_4);
undefined8 *FUN_140074c80(undefined8 *param_1,undefined8 param_2,int param_3,ulonglong *param_4,longlong param_5);
undefined8 * FUN_140074db0(undefined8 *param_1,int *param_2,int param_3);
void FUN_140074f30(undefined8 *param_1,ULONG_PTR param_2,ULONG_PTR param_3,ULONG_PTR param_4,ULONG_PTR param_5,ULONG_PTR param_6,int param_7,undefined8 param_8,undefined8 param_9,undefined8 *param_10,byte param_11);
void FUN_140075060(undefined8 *param_1,ULONG_PTR param_2,ULONG_PTR param_3,ULONG_PTR param_4,ULONG_PTR param_5,undefined8 param_6,int param_7,int param_8,longlong param_9,undefined8 *param_10,byte param_11);
void FUN_1400751e0(longlong param_1,longlong param_2);
undefined8 * _CreateFrameInfo(undefined8 *param_1,undefined8 param_2);
void FUN_140075410(longlong param_1);
undefined8 FUN_140075470(void);
undefined8 FUN_140075490(void);
void FUN_1400754b0(undefined8 param_1);
void FUN_1400754d0(undefined8 param_1);
void FUN_1400754f0(int *param_1,longlong param_2,ULONG_PTR param_3,ulonglong *param_4);
void FUN_140075570(int *param_1,longlong param_2,ULONG_PTR param_3,ulonglong *param_4);
void FUN_140075630(longlong *param_1,longlong *param_2);
void FUN_1400756d0(undefined8 *param_1);
undefined8 FUN_140075710(void);
void FUN_140075720(void);
void __DestructExceptionObject(int *param_1);
void FUN_1400757b0(undefined8 param_1,undefined *UNRECOVERED_JUMPTABLE);
undefined8 FUN_1400757c0(longlong param_1);
longlong FUN_140075800(longlong param_1,int *param_2);
undefined8 FUN_140075830(undefined8 *param_1);
longlong FUN_1400758a0(void);
longlong FUN_1400758c0(void);
void Unwind@1400758e0(void);
char * FUN_1400758f0(ulonglong param_1,char param_2);
void FUN_140075980(longlong *param_1,byte *param_2);
bool FUN_140075a30(void);
uint FUN_140075a60(void);
undefined1 FUN_140075a90(char param_1);
undefined8 FUN_140075ab0(PEXCEPTION_RECORD param_1,PVOID param_2,longlong param_3,longlong *param_4);
uint FUN_140075cd0(longlong param_1,longlong param_2);
PVOID FUN_140075d20(void);
PVOID FUN_140075e00(void);
undefined4 FUN_140075e60(void);
undefined1 FUN_140075ed0(void);
int FUN_140075f00(longlong *param_1,ulonglong *param_2,longlong param_3);
undefined4 GetUnwindTryBlock(longlong *param_1,ulonglong *param_2,longlong param_3);
void FUN_140075fc0(longlong *param_1,undefined8 param_2,longlong param_3,undefined4 param_4);
void SetUnwindTryBlock(longlong *param_1,ulonglong *param_2,longlong param_3,int param_4);
undefined4 FUN_140076010(longlong param_1,ulonglong *param_2);
int FUN_140076080(longlong param_1,ulonglong *param_2);
undefined4 FUN_140076180(longlong param_1,longlong param_2,ulonglong param_3);
int FUN_1400761f0(longlong param_1,longlong param_2,ulonglong param_3);
ulonglong FUN_1400762e0(longlong param_1,longlong *param_2,uint *param_3,byte *param_4);
ulonglong FUN_1400764c0(longlong param_1,longlong *param_2,longlong param_3,byte *param_4);
void FUN_1400766b0(longlong param_1,longlong *param_2,uint *param_3,byte *param_4);
void FUN_140076770(longlong param_1,longlong *param_2,longlong param_3,byte *param_4);
void FUN_140076830(int *param_1,longlong *param_2,ULONG_PTR param_3,ulonglong *param_4,uint *param_5,byte param_6);
void FUN_140076de0(int *param_1,longlong *param_2,ULONG_PTR param_3,ulonglong *param_4,byte *param_5,byte param_6);
void FUN_140077390(int *param_1,longlong *param_2,ULONG_PTR param_3,ulonglong *param_4,ULONG_PTR param_5,int param_6);
void FUN_1400775f0(int *param_1,longlong *param_2,ULONG_PTR param_3,undefined8 *param_4,byte *param_5,int param_6);
undefined8 FUN_140077960(byte *param_1,byte *param_2,uint *param_3);
undefined8 FUN_140077a90(longlong param_1,byte *param_2,uint *param_3);
undefined8 FUN_140077bc0(int *param_1,longlong *param_2,ULONG_PTR param_3,ulonglong *param_4,uint *param_5,int param_6,undefined8 param_7,byte param_8);
undefined8 FUN_140077df0(int *param_1,longlong *param_2,ULONG_PTR param_3,ulonglong *param_4,byte *param_5,int param_6,undefined8 param_7,byte param_8);
void FUN_140078050(int *param_1,longlong *param_2,ULONG_PTR param_3,ulonglong *param_4,uint *param_5,int param_6,undefined8 param_7,byte param_8);
ulonglong FUN_140078060(int *param_1,longlong *param_2,ULONG_PTR param_3,ulonglong *param_4,byte *param_5,int param_6,undefined8 param_7,byte param_8);
uint * FUN_1400780c0(uint *param_1,longlong param_2,longlong param_3);
undefined8 * FUN_1400781b0(undefined8 *param_1,longlong param_2);
undefined8 * FUN_1400781f0(undefined8 *param_1);
undefined8 FUN_140078220(longlong param_1);
longlong FUN_140078430(longlong param_1);
void FUN_140078690(longlong param_1);
undefined8 FUN_140078820(undefined8 *param_1,longlong param_2,undefined4 *param_3);
int __cdecl ExFilterRethrowFH4(_EXCEPTION_POINTERS *param_1,EHExceptionRecord *param_2,int param_3,int *param_4);
void FUN_1400788e0(longlong *param_1,ulonglong *param_2,longlong param_3,int param_4);
void FUN_140078a90(longlong *param_1,ulonglong *param_2,longlong param_3,int param_4);
int FUN_140078e70(undefined8 param_1,ulonglong *param_2,longlong param_3);
uint FUN_140078ee0(longlong param_1,longlong param_2);
undefined1 FUN_140078f20(longlong param_1,int *param_2);
ulonglong FUN_140079030(int *param_1);
void FUN_1400790c0(longlong param_1,longlong *param_2);
void FUN_140079160(undefined8 param_1,undefined *UNRECOVERED_JUMPTABLE,undefined8 param_3);
void FUN_140079170(undefined8 param_1,undefined *UNRECOVERED_JUMPTABLE,undefined8 param_3,undefined4 param_4);
int FUN_140079180(longlong *param_1,int param_2,longlong param_3,int param_4,longlong *param_5);
void FUN_140079260(longlong param_1,uint param_2);
undefined4 FUN_140079450(void);
undefined1 FUN_1400794e0(void);
void FUN_140079560(void);
void FUN_140079580(void);
void FUN_140079590(void);
void __except_validate_context_record(longlong param_1);
void _CallSettingFrame(void);
void FUN_140079660(void);
void FUN_140079690(void);
void _CallSettingFrameEncoded(undefined8 param_1,undefined8 param_2,undefined8 param_3);
_LocaleUpdate * __thiscall _LocaleUpdate::_LocaleUpdate(_LocaleUpdate *this,__crt_locale_pointers *param_1);
uint FUN_1400797a4(uint param_1,__crt_locale_pointers *param_2);
uint FUN_1400798d8(uint param_1,__crt_locale_pointers *param_2);
int __cdecl tolower(int _C);
int __cdecl toupper(int _C);
ulonglong FUN_140079a64(FILE *param_1,longlong *param_2);
ulonglong FUN_140079ae0(FILE *param_1,longlong *param_2);
longlong FUN_140079b88(longlong *param_1);
ulonglong FUN_140079bf0(FILE *param_1);
void FUN_140079c88(undefined8 *param_1);
void __cdecl common_end_thread(uint param_1);
undefined8 * FUN_140079d54(LPCWSTR param_1,undefined8 param_2);
HANDLE FUN_140079db8(LPSECURITY_ATTRIBUTES param_1,uint param_2,LPCWSTR param_3,undefined8 param_4,DWORD param_5,DWORD *param_6);
void FUN_140079e98(uint param_1);
undefined4 operator()<>(undefined8 param_1,longlong *param_2,undefined8 *param_3,longlong *param_4);
ulonglong FUN_140079ee4(ulonglong param_1,undefined1 *param_2,ulonglong param_3,longlong param_4,longlong *param_5,undefined8 param_6);
bool FUN_14007a094(longlong param_1,ulonglong param_2,longlong param_3);
uint FUN_14007a13c(longlong *param_1,longlong *param_2,uint param_3,byte param_4);
ulonglong FUN_14007a3f0(longlong param_1);
ulonglong FUN_14007a5f4(longlong param_1,byte param_2);
ulonglong FUN_14007a7f8(longlong param_1,byte param_2);
void FUN_14007a9fc(longlong param_1,uint param_2);
void FUN_14007aa6c(longlong param_1,uint param_2,byte param_3);
void FUN_14007ab18(longlong param_1,uint param_2,byte param_3);
void FUN_14007ab9c(longlong param_1,ulonglong param_2);
void FUN_14007ac0c(longlong param_1,ulonglong param_2,byte param_3);
void FUN_14007acbc(longlong param_1,ulonglong param_2,byte param_3);
undefined4 FUN_14007ad40(undefined8 *param_1);
void FUN_14007ae5c(char *param_1,longlong *param_2);
undefined1 FUN_14007aec8(longlong param_1,uint *param_2);
undefined4 FUN_14007af58(ulonglong *param_1);
undefined4 FUN_14007b2dc(ulonglong *param_1);
undefined4 FUN_14007b644(ulonglong *param_1);
ulonglong FUN_14007b9b4(ulonglong *param_1);
ulonglong FUN_14007bb3c(ulonglong *param_1);
ulonglong FUN_14007bcc4(ulonglong *param_1);
undefined8 FUN_14007c1fc(ulonglong *param_1);
undefined8 FUN_14007c66c(longlong param_1);
undefined8 FUN_14007c6e8(ulonglong *param_1);
undefined8 FUN_14007c944(longlong param_1);
ulonglong FUN_14007ca18(longlong param_1);
undefined8 FUN_14007cad0(longlong param_1);
void FUN_14007cb70(longlong *param_1);
bool __cdecl __acrt_stdio_char_traits<char>::validate_stream_is_ansi_if_required(_iobuf *param_1);
void __thiscall __crt_stdio_output::string_output_adapter<char>::write_string(string_output_adapter<char> *this,char *param_1,int param_2,int *param_3,__crt_deferred_errno_cache *param_4);
void FUN_14007cd28(longlong *param_1,byte *param_2,int param_3,int *param_4,longlong param_5);
undefined4 FUN_14007cdf8(undefined8 param_1,longlong param_2,longlong param_3,undefined4 *param_4,undefined8 param_5);
ulonglong FUN_14007cf1c(ulonglong param_1,undefined1 *param_2,ulonglong param_3,ulonglong param_4,longlong param_5,undefined4 *param_6,undefined8 param_7);
ulonglong FUN_14007d0e0(ulonglong param_1,undefined1 *param_2,ulonglong param_3,longlong param_4,undefined4 *param_5,undefined8 param_6);
ulonglong FUN_14007d334(ulonglong param_1,undefined1 *param_2,ulonglong param_3,longlong param_4,undefined4 *param_5,undefined8 param_6);
void FUN_14007d448(void);
undefined8 FUN_14007d470(char *param_1,longlong param_2,longlong param_3);
ulonglong operator()<>(undefined8 param_1,longlong *param_2,undefined8 *param_3,longlong *param_4);
ulonglong FUN_14007d530(undefined8 *param_1);
ulonglong FUN_14007d5b0(undefined8 param_1,longlong param_2,longlong param_3,longlong param_4,longlong *param_5);
ulonglong FUN_14007d64c(WCHAR *param_1,ulonglong param_2,ulonglong param_3,FILE *param_4,longlong *param_5);
ulonglong FUN_14007d860(undefined8 param_1,longlong param_2,longlong param_3,longlong param_4);
_iobuf * __cdecl common_fsopen<char>(char *param_1,char *param_2,int param_3);
_iobuf * __cdecl common_fsopen<wchar_t>(wchar_t *param_1,wchar_t *param_2,int param_3);
void FUN_14007da8c(wchar_t *param_1,wchar_t *param_2);
_iobuf * __cdecl common_fsopen<wchar_t>(wchar_t *param_1,wchar_t *param_2,int param_3);
errno_t __cdecl fopen_s(FILE **_File,char *_Filename,char *_Mode);
void thunk_FUN_14008be60(LPVOID param_1);
int __cdecl islower(int _C);
int __cdecl isspace(int _C);
int __cdecl isupper(int _C);
ulonglong FUN_14007dd18(byte param_1,ulonglong param_2);
void parse_floating_point<>(_locale_t param_1,longlong *param_2,uint *param_3);
void parse_floating_point<>(_locale_t param_1,longlong *param_2,ulonglong *param_3);
int FUN_14007dea0(_locale_t param_1,longlong *param_2,int *param_3);
int FUN_14007e664(char *param_1,longlong *param_2,longlong param_3);
int FUN_14007e814(char *param_1,longlong *param_2,longlong param_3);
undefined8 FUN_14007ea78(char *param_1,longlong *param_2);
undefined8 FUN_14007eaf4(char *param_1,longlong *param_2);
void FUN_14007eb70(int param_1,floating_point_string *param_2,uint *param_3);
void FUN_14007ecd8(int param_1,floating_point_string *param_2,ulonglong *param_3);
ulonglong FUN_14007eeb0(longlong *param_1,longlong *param_2,uint param_3,byte param_4);
ulonglong FUN_14007f2e4(undefined4 *param_1,longlong *param_2,uint param_3,byte param_4);
bool FUN_14007f3e4(longlong param_1);
bool FUN_14007f488(longlong param_1);
ulonglong FUN_14007f530(byte *param_1,int param_2);
ulonglong FUN_14007f6d4(input_processor<> *param_1,int param_2);
ulonglong FUN_14007f8b4(longlong param_1,_locale_t param_2);
undefined8 FUN_14007f908(longlong *param_1);
ulonglong FUN_14007f96c(longlong param_1);
undefined8 FUN_14007fb34(undefined8 *param_1);
undefined8 FUN_14007fb70(undefined8 *param_1);
undefined8 FUN_14007fbac(ulonglong param_1,int param_2,ulonglong param_3,char param_4,undefined8 *param_5);
void FUN_14007ff64(uint *param_1,uint param_2,ulonglong param_3,byte param_4,undefined8 *param_5);
void FUN_1400800c4(uint *param_1,undefined8 *param_2);
SLD_STATUS __cdecl __crt_strtox::convert_hexadecimal_string_to_floating_type_common(floating_point_string *param_1,floating_point_value *param_2);
ulonglong FUN_140081e70(uint *param_1,uint *param_2);
char FUN_1400822fc(longlong param_1);
undefined4 FUN_1400823b0(input_processor<> *param_1);
bool __thiscall __crt_stdio_input::input_processor<>::process_conversion_specifier(input_processor<> *this);
bool __thiscall __crt_stdio_input::input_processor<>::process_floating_point_specifier(input_processor<> *this);
bool __thiscall __crt_stdio_input::input_processor<>::process_integer_specifier(input_processor<> *this,uint param_1,bool param_2);
undefined8 FUN_1400826ec(longlong param_1,byte param_2);
ulonglong FUN_140082774(input_processor<> *param_1);
bool __thiscall __crt_stdio_input::input_processor<>::process_string_specifier(input_processor<> *this,conversion_mode param_1);
ulonglong FUN_1400828b8(longlong param_1);
undefined8 FUN_140082a84(longlong param_1);
void FUN_140082b24(longlong param_1);
ulonglong FUN_140082c34(longlong param_1);
bool __thiscall __crt_stdio_input::input_processor<>::write_character(input_processor<> *this,wchar_t *param_1,__uint64 param_2,wchar_t **param_3,__uint64 *param_4,char param_5);
ulonglong FUN_140082e64(longlong param_1,undefined8 param_2);
undefined4 FUN_140082ee0(undefined8 param_1,undefined1 (*param_2) [32],ulonglong param_3,longlong param_4,__crt_locale_pointers *param_5,undefined8 param_6);
uint FUN_140083004(int param_1,uint param_2,_locale_t param_3);
errno_t __cdecl memcpy_s(void *_Dst,rsize_t _DstSize,void *_Src,rsize_t _MaxCount);
LPVOID _calloc_base(ulonglong param_1,ulonglong param_2);
longlong FUN_140083120(byte *param_1,byte *param_2);
longlong FUN_1400831d0(byte *param_1,byte *param_2);
undefined8 FUN_1400835a0(void);
ulonglong FUN_1400835e0(undefined8 param_1,int *param_2);
double FUN_1400836e0(undefined8 param_1,int *param_2);
uint FUN_1400838a4(longlong *param_1,longlong *param_2,uint param_3,uint param_4);
uint FUN_140083b80(longlong *param_1,longlong *param_2,uint param_3,uint param_4);
ulonglong FUN_14008431c(longlong *param_1,longlong *param_2,uint param_3,byte param_4);
ulonglong FUN_1400845f8(longlong *param_1,longlong *param_2,uint param_3,byte param_4);
ulonglong FUN_140084d7c(longlong param_1);
uint FUN_140084e30(longlong param_1,undefined8 param_2,undefined8 param_3,undefined4 param_4);
ulonglong FUN_140084ee0(byte param_1,FILE *param_2,longlong *param_3);
ulonglong FUN_140085044(byte param_1,longlong *param_2);
ulonglong FUN_14008505c(byte param_1,FILE *param_2);
void FUN_1400850f4(undefined8 param_1,longlong *param_2,undefined8 *param_3,longlong *param_4);
void FUN_140085190(undefined8 param_1,int *param_2,undefined8 *param_3,int *param_4);
int FUN_140085274(undefined8 param_1,longlong *param_2,undefined8 *param_3,longlong *param_4);
int __cdecl common_flush_all(bool param_1);
undefined8 FUN_140085314(FILE *param_1,longlong *param_2);
int FUN_1400853a0(FILE *param_1);
int __cdecl common_flush_all(bool param_1);
int __cdecl fflush(FILE *_File);
ulonglong _fgetc_nolock(FILE *param_1);
ulonglong FUN_140085528(FILE *param_1);
undefined8 FUN_14008563c(void);
undefined * __acrt_iob_func(ulonglong param_1);
void __acrt_uninitialize_stdio(bool param_1);
undefined8 _get_stream_buffer_pointers(longlong param_1,longlong *param_2,longlong *param_3,longlong *param_4);
void FUN_140085814(longlong param_1);
void FUN_140085820(longlong param_1);
int __cdecl fgetpos(FILE *_File,fpos_t *_Pos);
ulonglong operator()<>(undefined8 param_1,longlong *param_2,undefined8 *param_3,longlong *param_4);
undefined8 FUN_1400858b8(undefined8 *param_1);
ulonglong FUN_1400859a0(longlong param_1,undefined8 param_2,int param_3,longlong param_4);
uint FUN_140085ad8(uint param_1,FILE *param_2);
int __cdecl ungetc(int _Ch,FILE *_File);
int __cdecl fsetpos(FILE *_File,fpos_t *_Pos);
ulonglong FUN_140085c88(undefined1 (*param_1) [32],ulonglong param_2,ulonglong param_3,ulonglong param_4,FILE *param_5);
size_t __cdecl fread(void *_DstBuf,size_t _ElementSize,size_t _Count,FILE *_File);
size_t __cdecl fread_s(void *_DstBuf,size_t _DstSize,size_t _ElementSize,size_t _Count,FILE *_File);
int FUN_140085fbc(FILE *param_1,LARGE_INTEGER param_2,uint param_3,longlong *param_4);
ulonglong FUN_140086058(longlong *param_1,ulonglong param_2,int param_3);
int FUN_140086154(FILE *param_1,LARGE_INTEGER param_2,DWORD param_3,longlong *param_4);
int FUN_140086240(FILE *param_1,LARGE_INTEGER param_2,uint param_3);
int FUN_1400862d8(FILE *param_1,int param_2,uint param_3);
WCHAR FUN_140086374(WCHAR param_1,__crt_locale_pointers *param_2);
void FUN_140086470(WCHAR param_1);
uint FUN_140086478(uint param_1);
void FUN_140086548(uint param_1);
void FUN_140086570(uint param_1,longlong param_2);
__acrt_ptd * FUN_140086594(void);
__acrt_ptd * FUN_1400865b8(void);
uint FUN_1400865dc(longlong param_1,longlong *param_2,uint param_3,undefined4 param_4);
ulonglong FUN_140086690(longlong param_1,longlong *param_2,uint param_3);
undefined8 FUN_140086750(longlong param_1,longlong param_2);
void FUN_140086780(ushort *param_1,ushort *param_2);
void FUN_1400867e0(WCHAR *param_1,WCHAR *param_2);
int FUN_140086810(WCHAR *param_1,WCHAR *param_2,undefined8 *param_3);
ulonglong FUN_1400869d4(FILE *param_1,longlong *param_2);
LARGE_INTEGER FUN_140086a64(FILE *param_1,longlong *param_2);
LARGE_INTEGER FUN_140086ad0(FILE *param_1,longlong *param_2);
LARGE_INTEGER FUN_140086c20(FILE *param_1,LARGE_INTEGER param_2,longlong param_3,longlong *param_4);
LARGE_INTEGER FUN_140086d7c(FILE *param_1,LARGE_INTEGER param_2,longlong *param_3);
longlong FUN_140086f04(short *param_1,short *param_2,char param_3);
LARGE_INTEGER FUN_140086f5c(FILE *param_1);
LARGE_INTEGER thunk_FUN_140086ad0(FILE *param_1,longlong *param_2);
ulonglong FUN_140087000(FILE *param_1);
void FUN_1400870a0(undefined1 *param_1,ulonglong param_2,ulonglong param_3,undefined *param_4);
LPVOID _malloc_base(ulonglong param_1);
ulonglong __acrt_initialize_locks(void);
void __acrt_lock(int param_1);
undefined8 __acrt_uninitialize_locks(void);
void __acrt_unlock(int param_1);
void FUN_140087500(void);
void __cdecl abort(void);
void FUN_140087584(undefined8 param_1,int *param_2,undefined8 *param_3,int *param_4);
void __thiscall __crt_seh_guarded_call<void>::operator()<>(__crt_seh_guarded_call<void> *this,<> *param_1,<> *param_2,<> *param_3);
void __thiscall __crt_seh_guarded_call<void>::operator()<>(__crt_seh_guarded_call<void> *this,<> *param_1,<> *param_2,<> *param_3);
void __thiscall <>::operator()(<> *this);
void FUN_1400877ec(undefined8 *param_1,longlong param_2,longlong param_3);
short * FUN_140087864(undefined1 (*param_1) [32]);
undefined4 FUN_1400878e8(void);
void __acrt_uninitialize_locale(void);
void FUN_140087924(short *param_1,longlong param_2,longlong param_3);
undefined8 FUN_1400879c8(undefined1 (*param_1) [32],ushort *param_2);
int __cdecl _configthreadlocale(int _Flag);
void FUN_140087b98(undefined8 *param_1,undefined8 *param_2);
short * FUN_140087c58(ushort *param_1,short *param_2,longlong param_3,short *param_4,longlong param_5,UINT *param_6);
void FUN_1400880dc(short *param_1,longlong param_2,int param_3,undefined8 param_4);
wchar_t * __cdecl _wsetlocale(int _Category,wchar_t *_Locale);
short * FUN_1400881e8(longlong param_1);
void FUN_1400883d4(longlong param_1,undefined8 param_2,wchar_t *param_3);
longlong FUN_14008866c(longlong param_1,undefined8 param_2,ushort *param_3);
ulonglong FUN_1400889d4(undefined1 (*param_1) [32],ushort *param_2);
uint FUN_140088bd0(longlong param_1,longlong *param_2);
ulonglong FUN_140088c18(short *param_1,longlong *param_2);
ulonglong FUN_140088ca4(longlong param_1,longlong *param_2);
ulonglong FUN_140088d98(longlong param_1,longlong *param_2);
undefined8 FUN_140088e28(longlong param_1,ulonglong param_2);
LPWSTR operator()<>(undefined8 param_1,int *param_2,undefined8 *param_3,int *param_4);
LPWSTR FUN_140088ed4(undefined8 *param_1);
char * __cdecl setlocale(int _Category,char *_Locale);
ushort * __cdecl __pctype_func(void);
undefined4 FUN_1400891f0(void);
longlong FUN_140089220(void);
undefined4 FUN_140089254(void);
LPCSTR FUN_140089284(longlong *param_1);
short * FUN_1400898d0(longlong param_1);
size_t __cdecl __strncnt(char *_String,size_t _Cnt);
uint FUN_140089988(LPCSTR param_1,longlong param_2,undefined8 param_3,UINT param_4);
ulonglong FUN_140089b30(undefined1 (*param_1) [32],ulonglong param_2);
ulonglong FUN_140089cc0(undefined1 (*param_1) [32]);
ulonglong FUN_140089e10(undefined1 (*param_1) [32],ulonglong param_2);
bool FUN_14008a020(undefined8 param_1);
void FUN_14008a060(undefined8 param_1);
ulonglong FUN_14008a070(void);
ulonglong _set_new_handler(ulonglong param_1);
undefined4 FUN_14008a11c(int param_1,undefined8 param_2);
void operator()<>(undefined8 param_1,int *param_2,undefined8 *param_3,int *param_4);
void FUN_14008a2c0(undefined8 *param_1);
void FUN_14008a380(uint param_1,undefined4 param_2,int param_3);
void FUN_14008a464(UINT param_1,char param_2);
void FUN_14008a494(uint param_1);
void FUN_14008a4f8(undefined8 param_1);
void FUN_14008a510(void);
void FUN_14008a520(uint param_1);
void FUN_14008a52c(ulonglong param_1);
void FUN_14008a568(uint param_1);
void FUN_14008a574(short *param_1,undefined8 *param_2,short *param_3,longlong *param_4,longlong *param_5);
LPVOID __acrt_allocate_buffer_for_argv(ulonglong param_1,ulonglong param_2,ulonglong param_3);
ulonglong FUN_14008a778(int param_1);
undefined8 FUN_14008a8f4(void);
undefined8 * FUN_14008a964(short *param_1);
void free_environment<>(undefined8 *param_1);
void uninitialize_environment_internal<>(undefined8 *param_1);
void uninitialize_environment_internal<>(undefined8 *param_1);
void FUN_14008ab00(void);
undefined8 thunk_FUN_14008a8f4(void);
ulonglong operator()<>(undefined8 param_1,int *param_2,undefined8 *param_3,int *param_4);
ulonglong operator()<>(undefined8 param_1,int *param_2,undefined8 *param_3,int *param_4);
undefined8 FUN_14008abc4(undefined8 *param_1);
undefined8 FUN_14008ad74(undefined8 *param_1);
void FUN_14008ae8c(undefined8 param_1);
void FUN_14008ae9c(undefined8 param_1);
undefined8 _initialize_onexit_table(longlong *param_1);
void _register_onexit_function(undefined8 param_1,undefined8 param_2);
undefined8 FUN_14008af64(void);
undefined1 FUN_14008af88(void);
undefined1 FUN_14008af98(void);
undefined8 FUN_14008afe0(void);
undefined1 FUN_14008b020(void);
void FUN_14008b07c(void);
undefined8 FUN_14008b090(bool param_1);
undefined4 FUN_14008b0c8(void);
void FUN_14008b0d0(undefined4 param_1);
undefined8 FUN_14008b0e0(void);
void FUN_14008b100(undefined8 param_1);
undefined8 FUN_14008b110(undefined8 param_1);
void FUN_14008b140(ulonglong param_1);
ushort * _get_wide_winmain_command_line(void);
void FUN_14008b1b8(undefined8 *param_1,undefined8 *param_2);
undefined8 FUN_14008b1f0(undefined8 *param_1,undefined8 *param_2);
undefined8 FUN_14008b22c(undefined4 *param_1);
errno_t __cdecl _set_fmode(int _Mode);
undefined8 FUN_14008b29c(uint param_1,int param_2);
undefined4 FUN_14008b380(void);
uint FUN_14008b390(uint param_1);
undefined4 * FUN_14008b3bc(void);
void FUN_14008b3d0(longlong *param_1,ushort *param_2,uint param_3,char *param_4,int param_5,undefined8 param_6,int param_7,UINT param_8,int param_9);
void __acrt_LCMapStringA(__crt_locale_pointers *param_1,ushort *param_2,uint param_3,char *param_4,int param_5,undefined8 param_6,int param_7,UINT param_8,int param_9);
void operator()<>(undefined8 param_1,int *param_2,undefined8 *param_3,int *param_4);
void operator()<>(undefined8 param_1,int *param_2,undefined8 *param_3,int *param_4);
void operator()<>(undefined8 param_1,int *param_2,undefined8 *param_3,int *param_4);
void operator()<>(undefined8 param_1,int *param_2,undefined8 *param_3,int *param_4);
void __cdecl construct_ptd_array(__acrt_ptd *param_1);
void FUN_14008b994(__acrt_ptd *param_1);
void __cdecl destroy_ptd_array(__acrt_ptd *param_1);
__acrt_ptd * FUN_14008baac(void);
void __cdecl replace_current_thread_locale_nolock(__acrt_ptd *param_1,__crt_locale_data *param_2);
void FUN_14008bbcc(void);
void FUN_14008bbe8(void);
__acrt_ptd * FUN_14008bc2c(void);
__acrt_ptd * FUN_14008bcbc(undefined8 param_1,longlong param_2);
ulonglong FUN_14008bd18(void);
undefined4 FUN_14008bd5c(void);
void __acrt_update_locale_info(longlong param_1,longlong *param_2);
void FUN_14008bdb4(longlong param_1,longlong *param_2,longlong param_3);
void FUN_14008bdec(longlong param_1,longlong *param_2);
void FUN_14008be20(longlong param_1,longlong *param_2,longlong param_3);
void FUN_14008be60(LPVOID param_1);
__acrt_ptd * FUN_14008beb8(longlong *param_1);
longlong FUN_14008bf24(longlong param_1,longlong param_2);
void FUN_14008bf70(int param_1,DWORD param_2,DWORD param_3);
void FUN_14008c0e0(undefined8 param_1);
void FUN_14008c0e8(wchar_t *param_1,wchar_t *param_2,wchar_t *param_3,uint param_4,uintptr_t param_5);
void FUN_14008c184(wchar_t *param_1,wchar_t *param_2,wchar_t *param_3,uint param_4,uintptr_t param_5,longlong *param_6);
void FUN_14008c23c(void);
void __cdecl _invoke_watson(wchar_t *param_1,wchar_t *param_2,wchar_t *param_3,uint param_4,uintptr_t param_5);
int __cdecl _fileno(FILE *_File);
ulonglong FUN_14008c2cc(undefined8 param_1,uint *param_2,undefined8 *param_3,uint *param_4);
ulonglong FUN_14008c344(uint param_1,longlong *param_2);
ulonglong FUN_14008c408(uint param_1);
undefined8 FUN_14008c4a0(uint param_1,longlong param_2);
undefined8 * FUN_14008c570(undefined8 *param_1);
void __cdecl __acrt_stdio_free_stream(undefined8 *param_1);
longlong * FUN_14008c5e4(longlong *param_1);
void __acrt_stdio_free_buffer_nolock(undefined8 *param_1);
LPVOID _calloc_base(ulonglong param_1,ulonglong param_2);
BOOL FUN_14008c7ac(undefined8 param_1,int *param_2,undefined8 *param_3,int *param_4);
FARPROC FUN_14008c808(void);
FARPROC FUN_14008c840(uint param_1,LPCSTR param_2,uint *param_3,uint *param_4);
INT_PTR FUN_14008c9f4(undefined8 param_1);
INT_PTR FUN_14008ca50(undefined8 param_1);
INT_PTR FUN_14008caac(void);
void FUN_14008caf8(undefined8 param_1,uint param_2,undefined8 param_3,undefined8 param_4);
DWORD __stdcall FlsAlloc(PFLS_CALLBACK_FUNCTION lpCallback);
BOOL __stdcall FlsFree(DWORD dwFlsIndex);
PVOID __stdcall FlsGetValue(DWORD dwFlsIndex);
void FUN_14008cbb4(void);
BOOL __stdcall FlsSetValue(DWORD dwFlsIndex,PVOID lpFlsData);
void FUN_14008cbc8(ushort *param_1,uint param_2,LPWSTR param_3,uint param_4);
void FUN_14008cc5c(short *param_1,uint param_2);
BOOL __stdcall InitializeCriticalSectionEx(LPCRITICAL_SECTION lpCriticalSection,DWORD dwSpinCount,DWORD Flags);
void FUN_14008ccd4(ushort *param_1);
void FUN_14008cd3c(uint param_1,short *param_2,uint param_3,uint param_4);
void FUN_14008cdc4(ushort *param_1,uint param_2,LPCWSTR param_3,uint param_4,LPWSTR param_5,int param_6,undefined8 param_7,undefined8 param_8,undefined8 param_9);
void FUN_14008ceb8(ushort *param_1,uint param_2);
INT_PTR FUN_14008cf1c(uint param_1);
void FUN_14008cf6c(void);
bool FUN_14008cfb0(void);
void FUN_14008cfc4(void);
bool FUN_14008d16c(void);
bool FUN_14008d19c(void);
undefined8 __acrt_uninitialize_winapi_thunks(char param_1);
void FUN_14008d224(void);
bool FUN_14008d264(void);
LPVOID _malloc_base(ulonglong param_1);
void FUN_14008d300(double *param_1,undefined1 (*param_2) [16],ulonglong param_3,char *param_4,longlong param_5,uint param_6,byte param_7,int param_8,__acrt_rounding_mode param_9,longlong *param_10);
void FUN_14008d694(ulonglong *param_1,undefined1 *param_2,ulonglong param_3,char *param_4,longlong param_5,int param_6,char param_7,int param_8,int param_9,longlong *param_10);
undefined8 FUN_14008d798(undefined1 *param_1,ulonglong param_2,int param_3,char param_4,int param_5,int *param_6,byte param_7,longlong *param_8);
void FUN_14008d97c(ulonglong *param_1,undefined8 *param_2,longlong param_3,char *param_4,longlong param_5,int param_6,int param_7,longlong *param_8);
undefined8 FUN_14008da54(undefined8 *param_1,undefined8 param_2,int param_3,int *param_4,char param_5,longlong *param_6);
void FUN_14008dba4(ulonglong *param_1,undefined8 *param_2,ulonglong param_3,char *param_4,longlong param_5,int param_6,char param_7,int param_8,int param_9,longlong *param_10);
bool __cdecl should_round_up(double *param_1,__uint64 param_2,short param_3,__acrt_rounding_mode param_4);
void FUN_14008de10(double *param_1,undefined1 (*param_2) [16],ulonglong param_3,char *param_4,longlong param_5,int param_6,uint param_7,ulonglong param_8,__acrt_rounding_mode param_9,longlong *param_10);
undefined4 FUN_14008e0f8(int *param_1,undefined1 (*param_2) [32],ulonglong param_3,WCHAR param_4,longlong *param_5);
int FUN_14008e2a8(LPWSTR param_1,byte *param_2,ulonglong param_3,longlong *param_4);
int FUN_14008e420(LPWSTR param_1,byte *param_2,ulonglong param_3,undefined4 *param_4);
ulonglong FUN_14008e4c4(byte *param_1);
ulonglong FUN_14008e4fc(ulonglong param_1,byte *param_2,ulonglong param_3,uint *param_4,longlong param_5);
undefined8 FUN_14008e6bc(byte *param_1,uint param_2,int *param_3,longlong param_4);
void FUN_14008e760(undefined2 *param_1,byte *param_2,ulonglong param_3,uint *param_4,longlong param_5);
byte * FUN_14008e7a8(ushort *param_1,undefined8 *param_2,ulonglong param_3,uint *param_4,longlong param_5);
bool FUN_14008e960(void);
ulonglong FUN_14008e978(FILE *param_1);
ulonglong FUN_14008e9c4(FILE *param_1);
void FUN_14008ea8c(char param_1,FILE *param_2,longlong *param_3);
void FUN_14008eacc(void);
void FUN_14008ebcc(void);
bool __acrt_initialize_lowio(void);
undefined1 __acrt_uninitialize_lowio(void);
DWORD * FUN_14008ed50(DWORD *param_1,uint param_2,byte *param_3,ulonglong param_4,longlong *param_5);
DWORD * FUN_14008f1e4(DWORD *param_1,uint param_2,char *param_3,ulonglong param_4);
DWORD * FUN_14008f2ec(DWORD *param_1,uint param_2,short *param_3,ulonglong param_4);
DWORD * FUN_14008f408(DWORD *param_1,uint param_2,WCHAR *param_3,uint param_4);
int FUN_14008f57c(uint param_1,WCHAR *param_2,uint param_3);
int FUN_14008f614(uint param_1,WCHAR *param_2,uint param_3,longlong *param_4);
int FUN_14008f734(uint param_1,WCHAR *param_2,uint param_3,longlong *param_4);
bool FUN_14008fa64(undefined1 param_1,FILE *param_2,longlong *param_3);
bool FUN_14008fb5c(longlong *param_1);
ulonglong FUN_14008fbe4(byte param_1,FILE *param_2,longlong *param_3);
char * __cdecl __acrt_stdio_parse_mode<char>(char *param_1);
wchar_t * __cdecl __acrt_stdio_parse_mode<wchar_t>(wchar_t *param_1);
undefined8 * FUN_140090278(char *param_1,undefined8 param_2,int param_3,undefined8 *param_4);
undefined8 * FUN_140090314(char *param_1,undefined8 param_2,int param_3,undefined8 *param_4);
int __cdecl _isctype_l(int _C,int _Type,_locale_t _Locale);
int __cdecl iswctype(wint_t _C,wctype_t _Type);
void fegetround(void);
undefined8 FUN_140090550(uint param_1,int param_2,undefined8 param_3,double param_4,ulonglong param_5);
bool FUN_1400906d0(uint param_1,double *param_2,ulonglong param_3);
void FUN_140090a50(uint *param_1,ulonglong *param_2,ulonglong param_3,int param_4,uint *param_5,uint *param_6);
void FUN_140090a80(uint *param_1,ulonglong *param_2,ulonglong param_3,int param_4,uint *param_5,uint *param_6,int param_7);
void FUN_140090d90(uint *param_1,ulonglong *param_2,ulonglong param_3,int param_4,uint *param_5,uint *param_6);
void FUN_140090dc0(int param_1);
undefined8 FUN_140090e00(uint param_1,int param_2,undefined8 param_3,undefined8 param_4,undefined8 param_5,uint param_6);
uint FUN_140090f60(void);
uint FUN_140090f80(uint param_1,uint param_2);
undefined4 FUN_140091000(void);
void FUN_140091010(undefined4 param_1);
void FUN_140091020(uint param_1);
uint FUN_140091040(void);
undefined8 FUN_140091050(undefined8 param_1,uint *param_2,undefined8 *param_3,uint *param_4);
int __cdecl _commit(int _FileHandle);
ulonglong FUN_140091170(FILE *param_1);
int FUN_1400912dc(void);
void __acrt_stdio_allocate_buffer_nolock(undefined8 *param_1);
int FUN_1400913fc(uint param_1,short *param_2,longlong param_3);
int FUN_1400915f8(uint param_1,byte *param_2,longlong param_3,LPWSTR param_4,int param_5);
int FUN_140091928(uint param_1,LPWSTR param_2,uint param_3);
int FUN_140091a48(uint param_1,LPWSTR param_2,uint param_3);
longlong FUN_140091ea8(uint param_1,LARGE_INTEGER param_2,DWORD param_3,longlong *param_4);
longlong FUN_140091fc8(uint param_1,LARGE_INTEGER param_2,DWORD param_3,longlong param_4);
longlong FUN_140092078(uint param_1,LARGE_INTEGER param_2,DWORD param_3);
longlong thunk_FUN_140091ea8(uint param_1,LARGE_INTEGER param_2,DWORD param_3,longlong *param_4);
longlong FUN_14009211c(uint param_1,LARGE_INTEGER param_2,DWORD param_3);
longlong thunk_FUN_140091fc8(uint param_1,LARGE_INTEGER param_2,DWORD param_3,longlong param_4);
void __acrt_LCMapStringW(ushort *param_1,uint param_2,undefined1 (*param_3) [32],uint param_4,LPWSTR param_5,int param_6);
void FUN_140092248(void);
ulonglong FUN_140092260(undefined8 param_1,int *param_2,undefined8 param_3,int *param_4);
void __acrt_get_sigabrt_handler(void);
void FUN_1400922d8(undefined8 param_1);
undefined8 FUN_1400922f8(uint param_1);
void __acrt_locale_free_monetary(longlong param_1);
undefined8 FUN_140092660(longlong param_1);
void __acrt_locale_free_numeric(longlong *param_1);
undefined8 FUN_140092be8(longlong param_1);
void FUN_140092ed0(undefined8 *param_1,longlong param_2);
bool __cdecl initialize_lc_time(__crt_lc_time_data *param_1,__crt_locale_data *param_2);
void __acrt_locale_free_time(undefined8 *param_1);
undefined8 __acrt_locale_initialize_time(__crt_locale_data *param_1);
undefined8 FUN_1400933f0(short *param_1,longlong param_2,longlong param_3);
undefined8 FUN_140093480(short *param_1,longlong param_2,longlong param_3);
undefined8 FUN_140093510(short *param_1,longlong param_2,short *param_3,longlong param_4);
undefined4 FUN_140093650(short *param_1,longlong param_2,longlong param_3,longlong param_4);
longlong FUN_1400937a0(ushort *param_1,ushort *param_2);
int __cdecl wcsncmp(wchar_t *_Str1,wchar_t *_Str2,size_t _MaxCount);
ushort * FUN_140093840(ushort *param_1,ushort *param_2);
BOOL FUN_140093898(__crt_locale_pointers *param_1,DWORD param_2,LPCSTR param_3,int param_4,LPWORD param_5,UINT param_6,int param_7);
void FUN_140093a28(UINT param_1,DWORD param_2,LPCSTR param_3,int param_4,LPWSTR param_5,int param_6);
void __acrt_add_locale_ref(longlong param_1);
void __acrt_free_locale(longlong param_1);
int __acrt_locale_add_lc_time_reference(undefined **param_1);
void __acrt_locale_free_lc_time_if_unreferenced(undefined **param_1);
int __acrt_locale_release_lc_time_reference(undefined **param_1);
void __acrt_release_locale_ref(longlong param_1);
undefined ** __acrt_update_thread_locale_data(void);
undefined ** _updatetlocinfoEx_nolock(longlong *param_1,undefined **param_2);
void FUN_140093ec4(undefined8 param_1,int *param_2,undefined8 *param_3,int *param_4);
int __cdecl getSystemCP(int param_1);
void FUN_140094114(longlong param_1);
void FUN_1400941ac(longlong param_1);
ulonglong FUN_1400943a4(int param_1,char param_2,__acrt_ptd *param_3,__crt_multibyte_data **param_4);
__crt_multibyte_data * __cdecl update_thread_multibyte_data_internal(__acrt_ptd *param_1,__crt_multibyte_data **param_2);
undefined8 __acrt_initialize_multibyte(void);
void FUN_14009472c(void);
undefined8 FUN_140094748(int param_1,longlong param_2);
void FUN_140094a10(longlong param_1);
void GetLocaleNameFromLangCountry(undefined8 *param_1);
void GetLocaleNameFromLanguage(undefined8 *param_1);
uint FUN_140094c20(wchar_t *param_1);
uint FUN_140094f14(ushort *param_1);
void FUN_140095000(WCHAR *param_1,longlong param_2,undefined8 param_3,undefined4 param_4);
bool FUN_1400950c0(wchar_t *param_1);
bool TranslateName(longlong param_1,int param_2,longlong *param_3);
undefined8 FUN_1400951c8(short *param_1,uint *param_2,LPWSTR param_3,undefined4 param_4);
uint FUN_140095444(ushort *param_1);
void GetLcidFromLangCountry(uint *param_1);
void GetLcidFromLanguage(byte *param_1);
uint FUN_140095694(ushort *param_1);
uint FUN_1400958d8(ushort *param_1);
int FUN_1400959e0(ushort *param_1);
UINT FUN_140095a30(ushort *param_1,longlong param_2,undefined8 param_3,undefined4 param_4);
bool FUN_140095ae4(uint param_1,int param_2);
ulonglong FUN_140095ba0(longlong param_1,int param_2,longlong *param_3);
undefined8 FUN_140095c28(longlong param_1,UINT *param_2,LPWSTR param_3,undefined4 param_4);
undefined4 FUN_140095ea8(undefined8 *param_1,LPWSTR param_2,byte *param_3,byte *param_4,byte *param_5,longlong *param_6);
byte * FUN_140095fc8(LPWSTR param_1,byte *param_2,byte *param_3,longlong *param_4);
undefined4 FUN_1400961c0(undefined8 *param_1,LPWSTR param_2,byte *param_3,byte *param_4,byte *param_5);
undefined4 FUN_140096268(ulonglong *param_1,byte *param_2,ulonglong param_3,LPCWSTR param_4,ulonglong param_5,longlong *param_6);
ulonglong FUN_14009637c(byte *param_1,LPCWSTR param_2,ulonglong param_3,longlong *param_4);
undefined4 FUN_1400966bc(ulonglong *param_1,byte *param_2,ulonglong param_3,LPCWSTR param_4,ulonglong param_5,undefined4 *param_6);
undefined4 FUN_140096774(__crt_locale_pointers *param_1,ushort *param_2,uint param_3,ulonglong param_4,int param_5);
LPWSTR FUN_1400968f0(__crt_locale_pointers *param_1,int param_2,ushort *param_3,uint param_4,longlong *param_5);
LPVOID _realloc_base(LPVOID param_1,ulonglong param_2);
void FUN_140096b3c(uint param_1,uint param_2,LPCWSTR param_3,int param_4,LPSTR param_5,int param_6,LPBOOL param_7,LPBOOL param_8);
uint FUN_140096c6c(void);
uint FUN_140096c88(void);
ulonglong FUN_140096cb4(undefined8 *param_1,undefined8 *param_2);
undefined8 FUN_1400970b4(longlong param_1,longlong param_2,ulonglong param_3,longlong *param_4);
ulonglong thunk_FUN_140096cb4(undefined8 *param_1,undefined8 *param_2);
undefined1 FUN_140097244(void);
undefined8 * FUN_14009726c(void);
LPVOID _recalloc_base(LPVOID param_1,ulonglong param_2,ulonglong param_3);
bool FUN_1400973d0(void);
undefined8 FUN_140097428(undefined8 *param_1,undefined8 *param_2);
undefined8 FUN_1400974a8(longlong param_1,longlong param_2);
undefined8 * __acrt_lowio_create_handle_array(void);
void __acrt_lowio_destroy_handle_array(LPCRITICAL_SECTION param_1);
longlong __acrt_lowio_ensure_fh_exists(uint param_1);
void __acrt_lowio_lock_fh(uint param_1);
undefined8 FUN_1400976ac(uint param_1,HANDLE param_2);
void __acrt_lowio_unlock_fh(uint param_1);
int __cdecl _alloc_osfhnd(void);
undefined8 FUN_1400978d4(uint param_1);
undefined8 FUN_140097990(uint param_1);
int FUN_140097a20(uint param_1,short *param_2,int param_3);
undefined4 FUN_140097b08(ushort *param_1);
bool FUN_140097bb0(char *param_1,char *param_2,int param_3,int param_4,int param_5);
undefined4 FUN_140097c80(char *param_1,ulonglong param_2,int param_3,int *param_4,int param_5,int param_6,longlong *param_7);
undefined8 FUN_140097d90(ulonglong param_1,int param_2,uint param_3,undefined4 *param_4,char *param_5,longlong param_6);
undefined8 FUN_140099010(byte *param_1,uint param_2,undefined8 *param_3,longlong param_4);
undefined8 FUN_1400990b4(undefined8 param_1,undefined8 *param_2);
undefined8 FUN_1400990bc(undefined8 *param_1,longlong param_2);
byte FUN_1400990d0(uint param_1);
undefined2 FUN_140099130(undefined2 param_1);
int FUN_140099170(ushort *param_1,ushort *param_2,longlong param_3);
void FUN_1400991e0(WCHAR *param_1,WCHAR *param_2,longlong param_3);
int FUN_140099210(WCHAR *param_1,WCHAR *param_2,longlong param_3,undefined8 *param_4);
undefined8 FUN_1400993f0(longlong param_1,longlong param_2,ulonglong param_3);
ulonglong FUN_140099430(byte *param_1,byte *param_2,ulonglong param_3);
int FUN_1400994c0(byte *param_1,byte *param_2,ulonglong param_3,undefined8 *param_4);
int common_sopen_dispatch<>(LPCSTR param_1,uint param_2,int param_3,ulonglong param_4,uint *param_5,int param_6);
int common_sopen_dispatch<>(LPCWSTR param_1,uint param_2,int param_3,ulonglong param_4,uint *param_5,int param_6);
undefined4 FUN_1400997a4(uint param_1,byte *param_2,uint param_3,char *param_4);
byte * FUN_140099a24(byte *param_1,uint param_2,int param_3,byte param_4);
int __cdecl truncate_ctrl_z_if_present(int param_1);
int FUN_140099cfc(undefined4 *param_1,uint *param_2,LPCSTR param_3,uint param_4,int param_5,byte param_6);
errno_t __cdecl FID_conflict:_sopen_s(int *_FileHandle,char *_Filename,int _OpenFlag,int _ShareFlag,int _PermissionMode);
int FUN_140099e4c(undefined4 *param_1,uint *param_2,LPCWSTR param_3,uint param_4,int param_5,byte param_6);
errno_t __cdecl FID_conflict:_sopen_s(int *_FileHandle,char *_Filename,int _OpenFlag,int _ShareFlag,int _PermissionMode);
BOOL __stdcall GetStringTypeW(DWORD dwInfoType,LPCWSTR lpSrcStr,int cchSrc,LPWORD lpCharType);
uint FUN_14009a2a0(uint param_1);
uint FUN_14009a2b0(void);
uint FUN_14009a390(void);
void FUN_14009a3f0(uint param_1);
void FUN_14009a520(uint param_1);
uint FUN_14009a594(void);
undefined8 FUN_14009a5a8(uint param_1,short *param_2,ulonglong param_3,uint param_4,char param_5);
int __cdecl common_xtox_s<>(ulong param_1,wchar_t *param_2,__uint64 param_3,uint param_4,bool param_5);
errno_t __cdecl FID_conflict:_ltow_s(long _Val,wchar_t *_DstBuf,size_t _SizeInWords,int _Radix);
int FUN_14009a710(ushort *param_1,ushort *param_2);
longlong FUN_14009a748(byte *param_1,undefined8 *param_2,ulonglong param_3,int *param_4,longlong param_5);
undefined4 FUN_14009a8a0(char *param_1,longlong param_2,longlong param_3,longlong param_4);
undefined8 _msize_base(longlong param_1);
undefined8 FUN_14009aa20(uint *param_1,uint param_2,uint param_3);
undefined8 FUN_14009aa90(uint *param_1);
bool FUN_14009aab0(uint *param_1);
undefined8 FUN_14009ab20(ulonglong *param_1);
double FUN_14009ab80(void);
double FUN_14009ac50(void);
bool __dcrt_lowio_ensure_console_output_initialized(void);
void FUN_14009b250(void);
BOOL __dcrt_write_console(void *param_1,DWORD param_2,LPDWORD param_3);
undefined4 FUN_14009b32c(uint param_1,LARGE_INTEGER param_2);
undefined4 FUN_14009b3c4(uint param_1,LARGE_INTEGER param_2,longlong *param_3);
uint FUN_14009b560(void);
uint thunk_FUN_14009b5e0(uint param_1,uint param_2);
uint FUN_14009b5e0(uint param_1,uint param_2);
uint FUN_14009b880(uint param_1,ulonglong *param_2);
undefined8 FUN_14009b990(undefined8 param_1,int param_2,undefined8 param_3,int param_4,uint param_5,undefined8 param_6,undefined8 param_7,undefined8 param_8,int param_9);
float FUN_14009bad0(undefined8 param_1,int param_2,float param_3,int param_4,uint param_5,undefined8 param_6,float param_7,float param_8,int param_9);
ulonglong FUN_14009bc10(ulonglong param_1);
uint FUN_14009bc30(uint param_1);
undefined8 FUN_14009bc60(void);
void FUN_14009bd20(undefined8 param_1,undefined8 param_2,int param_3);
undefined8 FUN_14009bd40(undefined8 param_1,undefined8 param_2,int param_3,int param_4,undefined8 param_5);
longlong FUN_14009bde0(longlong param_1,ulonglong param_2);
ulonglong FUN_14009be30(longlong param_1);
bool FUN_14009be80(short *param_1);
undefined8 FUN_14009beb0(PEXCEPTION_RECORD param_1,PVOID param_2,longlong param_3,longlong *param_4);
undefined8 FUN_14009bf30(int *param_1,longlong param_2,ULONG_PTR param_3,ulonglong *param_4);
void FUN_14009bfa0(PEXCEPTION_RECORD param_1,PVOID param_2,longlong param_3,longlong *param_4);
void FUN_14009bff0(PVOID param_1,PVOID param_2);
undefined1 (*) [16] FUN_14009c020(undefined1 (*param_1) [16],byte param_2);
ushort * FUN_14009c160(ushort *param_1,ushort param_2);
void FUN_14009c1f0(float param_1);
float FUN_14009c210(float param_1);
ulonglong FUN_14009c430(void);
undefined8 FUN_14009c4d0(float param_1);
undefined8 FUN_14009c770(undefined8 param_1);
ulonglong FUN_14009c990(ulonglong param_1,float param_2);
undefined8 FUN_14009ccf0(undefined8 param_1);
undefined8 FUN_14009d060(undefined8 param_1);
float FUN_14009d3b0(float param_1);
ulonglong FUN_14009d450(float param_1);
ulonglong FUN_14009d650(ulonglong param_1);
ulonglong FUN_14009d7a0(ulonglong param_1);
uint FUN_14009d990(void);
void FUN_14009da10(float param_1);
void FUN_14009daa0(float param_1);
void __remainder_piby2d2f_forC(ulonglong param_1,double *param_2,uint *param_3);
uint FUN_14009dcc0(uint param_1);
void _guard_dispatch_icall(void);
void _guard_dispatch_icall(void);
void _guard_dispatch_icall(void);
void FUN_14009dd50(undefined1 *param_1,undefined1 *param_2,longlong param_3);
void FUN_14009dd60(undefined8 *param_1,undefined8 *param_2,ulonglong param_3);
char * FUN_14009e3f0(char *param_1,byte param_2,char *param_3);
int __cdecl memcmp(void *_Buf1,void *_Buf2,size_t _Size);
undefined8 FUN_14009e580(undefined1 *param_1,undefined1 param_2,longlong param_3,undefined8 param_4);
void FUN_14009e590(undefined1 (*param_1) [32],byte param_2,ulonglong param_3);
int __cdecl strncmp(char *_Str1,char *_Str2,size_t _MaxCount);
int __cdecl strcmp(char *_Str1,char *_Str2);
size_t __cdecl strlen(char *_Str);
char * __cdecl strncpy(char *_Dest,char *_Source,size_t _Count);
void FUN_14009ed08(void);
void FUN_14009ee94(undefined8 param_1,longlong param_2);
void FUN_14009ef17(void);
undefined8 FUN_14009ef40(undefined8 param_1,longlong param_2);
void FUN_14009efa7(undefined8 param_1,longlong param_2);
void FUN_14009efcd(undefined8 param_1,longlong param_2);
void FUN_14009f022(undefined8 param_1,longlong param_2);
undefined8 FUN_14009f088(undefined8 param_1,longlong param_2);
undefined8 FUN_14009f0e3(void);
void FUN_14009f1ad(undefined8 param_1,longlong param_2);
undefined8 FUN_14009f1ee(undefined8 param_1,longlong param_2);
void FUN_14009f258(void);
void FUN_14009f278(void);
void FUN_14009f298(undefined8 param_1,longlong param_2);
void FUN_14009f2dc(undefined8 param_1,longlong param_2);
void FUN_14009f37a(undefined8 param_1,longlong param_2);
void FUN_14009f52f(undefined8 param_1,longlong param_2);
void FUN_14009f55b(undefined8 param_1,longlong param_2);
void FUN_14009f593(undefined8 param_1,longlong param_2);
void FUN_14009f5bf(undefined8 param_1,longlong param_2);
void FUN_14009f61b(undefined8 param_1,longlong param_2);
void FUN_14009f64a(undefined8 param_1,longlong param_2);
void FUN_14009f679(undefined8 param_1,longlong param_2);
void FUN_14009f6a8(undefined8 param_1,longlong param_2);
void FUN_14009f76a(undefined8 param_1,longlong param_2);
void FUN_14009f796(undefined8 param_1,longlong param_2);
void FUN_14009f7f2(undefined8 param_1,longlong param_2);
void FUN_14009f818(undefined8 param_1,longlong param_2);
void FUN_14009f876(undefined8 param_1,longlong param_2);
void FUN_14009f943(undefined8 param_1,longlong param_2);
void FUN_14009f969(undefined8 param_1,longlong param_2);
void FUN_14009f9b6(undefined8 param_1,longlong param_2);
undefined8 FUN_14009fa1b(undefined8 param_1,longlong param_2);
undefined8 FUN_14009fa76(undefined8 param_1,longlong param_2);
void FUN_14009fae9(undefined8 param_1,longlong param_2);
void FUN_14009fb57(undefined8 param_1,longlong param_2);
void FUN_14009fc71(undefined8 param_1,longlong param_2);
void FUN_14009fd0a(undefined8 param_1,longlong param_2);
void FUN_14009fd88(undefined8 param_1,longlong param_2);
void FUN_14009ffe6(undefined8 param_1,longlong param_2);
undefined8 FUN_1400a00c8(void);
void FUN_1400a015e(undefined8 param_1,longlong param_2);
void FUN_1400a01c0(undefined8 param_1,longlong param_2);
void FUN_1400a022f(void);
void FUN_1400a0617(void);
void FUN_1400a06a5(void);
void FUN_1400a06d0(void);
void FUN_1400a06f2(void);
void FUN_1400a07a2(void);
void FUN_1400a0920(undefined8 param_1,longlong param_2);
bool FUN_1400a0960(undefined8 *param_1);
void FUN_1400a0980(undefined8 param_1,longlong param_2);
undefined4 FUN_1400a09b0(undefined8 param_1,longlong param_2);
void FUN_1400a0a10(undefined8 *param_1);
undefined4 FUN_1400a0a30(undefined8 param_1,longlong param_2);
undefined4 FUN_1400a0ae0(undefined8 param_1,longlong param_2);
undefined4 FUN_1400a0ba0(undefined8 param_1,longlong param_2);
void FUN_1400a0c40(void);
void FUN_1400a0c60(undefined8 *param_1,longlong param_2);
void FUN_1400a0c81(undefined8 param_1,longlong param_2);
void FUN_1400a0d10(_EXCEPTION_POINTERS *param_1,longlong param_2);
void FUN_1400a0d35(undefined8 param_1,longlong param_2);
void FUN_1400a0dd0(undefined8 *param_1);
void FUN_1400a0de6(void);
void FUN_1400a0e10(undefined8 *param_1);
void FUN_1400a0e26(void);
void FUN_1400a0e49(undefined8 param_1,longlong param_2);
void FUN_1400a0e61(undefined8 *param_1);
void FUN_1400a0e7f(undefined8 param_1,longlong param_2);
void FUN_1400a0e9a(undefined8 param_1,longlong param_2);
void FUN_1400a0ec5(undefined8 param_1,longlong param_2);
void FUN_1400a0edf(undefined8 param_1,longlong param_2);
void FUN_1400a0efc(undefined8 param_1,longlong param_2);
void FUN_1400a0f16(undefined8 param_1,longlong param_2);
void FUN_1400a0f2e(undefined8 param_1,longlong param_2);
void FUN_1400a0f46(undefined8 param_1,longlong param_2);
void FUN_1400a0f60(undefined8 param_1,longlong param_2);
void FUN_1400a0f7a(undefined8 param_1,longlong param_2);
void FUN_1400a0fa0(void);
void FUN_1400a0fb6(void);
undefined4 FUN_1400a0fcc(undefined8 *param_1,longlong param_2);
void FUN_1400a0ff9(undefined8 param_1,longlong param_2);
void FUN_1400a1013(void);
void FUN_1400a102c(void);
void FUN_1400a1045(undefined8 param_1,longlong param_2);
undefined8 FUN_1400a1060(undefined8 *param_1,longlong param_2);
void FUN_1400a108d(undefined8 param_1,longlong param_2);
void FUN_1400a10a4(undefined8 param_1,longlong param_2);
void FUN_1400a10c5(void);
void FUN_1400a10de(undefined8 param_1,longlong param_2);
void FUN_1400a10f8(void);
void FUN_1400a1111(undefined8 param_1,longlong param_2);
undefined8 _ctrlfp$filt$0(undefined8 *param_1);
bool FUN_1400a11c0(undefined8 *param_1);
void FUN_1400a11ec(void);
void FUN_1400a129c(void);
void FUN_1400a12bc(void);
void FUN_1400a12e8(void);
void FUN_1400a1310(void);
void FUN_1400a1360(void);
void FUN_1400a13e0(void);
void FUN_1400a1440(void);
void FUN_1400a1490(void);

