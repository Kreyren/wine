/*
 * Unit tests for security functions
 *
 * Copyright (c) 2004 Mike McCormack
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <stdio.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "aclapi.h"
#include "winnt.h"
#include "sddl.h"
#include "ntsecapi.h"
#include "lmcons.h"
#include "winternl.h"

#include "wine/test.h"

typedef VOID (WINAPI *fnBuildTrusteeWithSidA)( PTRUSTEEA pTrustee, PSID pSid );
typedef VOID (WINAPI *fnBuildTrusteeWithNameA)( PTRUSTEEA pTrustee, LPSTR pName );
typedef VOID (WINAPI *fnBuildTrusteeWithObjectsAndNameA)( PTRUSTEEA pTrustee,
                                                          POBJECTS_AND_NAME_A pObjName,
                                                          SE_OBJECT_TYPE ObjectType,
                                                          LPSTR ObjectTypeName,
                                                          LPSTR InheritedObjectTypeName,
                                                          LPSTR Name );
typedef VOID (WINAPI *fnBuildTrusteeWithObjectsAndSidA)( PTRUSTEEA pTrustee,
                                                         POBJECTS_AND_SID pObjSid,
                                                         GUID* pObjectGuid,
                                                         GUID* pInheritedObjectGuid,
                                                         PSID pSid );
typedef LPSTR (WINAPI *fnGetTrusteeNameA)( PTRUSTEEA pTrustee );
typedef BOOL (WINAPI *fnConvertSidToStringSidA)( PSID pSid, LPSTR *str );
typedef BOOL (WINAPI *fnConvertStringSidToSidA)( LPCSTR str, PSID pSid );
typedef BOOL (WINAPI *fnGetFileSecurityA)(LPCSTR, SECURITY_INFORMATION,
                                          PSECURITY_DESCRIPTOR, DWORD, LPDWORD);
typedef DWORD (WINAPI *fnRtlAdjustPrivilege)(ULONG,BOOLEAN,BOOLEAN,PBOOLEAN);
typedef BOOL (WINAPI *fnCreateWellKnownSid)(WELL_KNOWN_SID_TYPE,PSID,PSID,DWORD*);

typedef NTSTATUS (WINAPI *fnLsaQueryInformationPolicy)(LSA_HANDLE,POLICY_INFORMATION_CLASS,PVOID*);
typedef NTSTATUS (WINAPI *fnLsaClose)(LSA_HANDLE);
typedef NTSTATUS (WINAPI *fnLsaFreeMemory)(PVOID);
typedef NTSTATUS (WINAPI *fnLsaOpenPolicy)(PLSA_UNICODE_STRING,PLSA_OBJECT_ATTRIBUTES,ACCESS_MASK,PLSA_HANDLE);
static NTSTATUS (WINAPI *pNtQueryObject)(HANDLE,OBJECT_INFORMATION_CLASS,PVOID,ULONG,PULONG);

static HMODULE hmod;
static int     myARGC;
static char**  myARGV;

fnBuildTrusteeWithSidA   pBuildTrusteeWithSidA;
fnBuildTrusteeWithNameA  pBuildTrusteeWithNameA;
fnBuildTrusteeWithObjectsAndNameA pBuildTrusteeWithObjectsAndNameA;
fnBuildTrusteeWithObjectsAndSidA pBuildTrusteeWithObjectsAndSidA;
fnGetTrusteeNameA pGetTrusteeNameA;
fnConvertSidToStringSidA pConvertSidToStringSidA;
fnConvertStringSidToSidA pConvertStringSidToSidA;
fnGetFileSecurityA pGetFileSecurityA;
fnRtlAdjustPrivilege pRtlAdjustPrivilege;
fnCreateWellKnownSid pCreateWellKnownSid;
fnLsaQueryInformationPolicy pLsaQueryInformationPolicy;
fnLsaClose pLsaClose;
fnLsaFreeMemory pLsaFreeMemory;
fnLsaOpenPolicy pLsaOpenPolicy;

struct sidRef
{
    SID_IDENTIFIER_AUTHORITY auth;
    const char *refStr;
};

static void init(void)
{
    HMODULE hntdll = GetModuleHandleA("ntdll.dll");

    hmod = GetModuleHandle("advapi32.dll");
    myARGC = winetest_get_mainargs( &myARGV );

    pNtQueryObject = (void *)GetProcAddress( hntdll, "NtQueryObject" );
}

static void test_str_sid(const char *str_sid)
{
    PSID psid;
    char *temp;

    if (pConvertStringSidToSidA(str_sid, &psid))
    {
        if (pConvertSidToStringSidA(psid, &temp))
        {
            trace(" %s: %s\n", str_sid, temp);
            LocalFree(temp);
        }
        LocalFree(psid);
    }
    else
    {
        if (GetLastError() != ERROR_INVALID_SID)
            trace(" %s: couldn't be converted, returned %d\n", str_sid, GetLastError());
        else
            trace(" %s: couldn't be converted\n", str_sid);
    }
}

static void test_sid(void)
{
    struct sidRef refs[] = {
     { { {0x00,0x00,0x33,0x44,0x55,0x66} }, "S-1-860116326-1" },
     { { {0x00,0x00,0x01,0x02,0x03,0x04} }, "S-1-16909060-1"  },
     { { {0x00,0x00,0x00,0x01,0x02,0x03} }, "S-1-66051-1"     },
     { { {0x00,0x00,0x00,0x00,0x01,0x02} }, "S-1-258-1"       },
     { { {0x00,0x00,0x00,0x00,0x00,0x02} }, "S-1-2-1"         },
     { { {0x00,0x00,0x00,0x00,0x00,0x0c} }, "S-1-12-1"        },
    };
    const char noSubAuthStr[] = "S-1-5";
    unsigned int i;
    PSID psid = NULL;
    BOOL r;
    LPSTR str = NULL;

    pConvertSidToStringSidA = (fnConvertSidToStringSidA)
                    GetProcAddress( hmod, "ConvertSidToStringSidA" );
    if( !pConvertSidToStringSidA )
        return;
    pConvertStringSidToSidA = (fnConvertStringSidToSidA)
                    GetProcAddress( hmod, "ConvertStringSidToSidA" );
    if( !pConvertStringSidToSidA )
        return;

    r = pConvertStringSidToSidA( NULL, NULL );
    ok( !r, "expected failure with NULL parameters\n" );
    if( GetLastError() == ERROR_CALL_NOT_IMPLEMENTED )
        return;
    ok( GetLastError() == ERROR_INVALID_PARAMETER,
     "expected GetLastError() is ERROR_INVALID_PARAMETER, got %d\n",
     GetLastError() );

    r = pConvertStringSidToSidA( refs[0].refStr, NULL );
    ok( !r && GetLastError() == ERROR_INVALID_PARAMETER,
     "expected GetLastError() is ERROR_INVALID_PARAMETER, got %d\n",
     GetLastError() );

    r = pConvertStringSidToSidA( NULL, &str );
    ok( !r && GetLastError() == ERROR_INVALID_PARAMETER,
     "expected GetLastError() is ERROR_INVALID_PARAMETER, got %d\n",
     GetLastError() );

    r = pConvertStringSidToSidA( noSubAuthStr, &psid );
    ok( !r,
     "expected failure with no sub authorities\n" );
    ok( GetLastError() == ERROR_INVALID_SID,
     "expected GetLastError() is ERROR_INVALID_SID, got %d\n",
     GetLastError() );

    for( i = 0; i < sizeof(refs) / sizeof(refs[0]); i++ )
    {
        PISID pisid;

        r = AllocateAndInitializeSid( &refs[i].auth, 1,1,0,0,0,0,0,0,0,
         &psid );
        ok( r, "failed to allocate sid\n" );
        r = pConvertSidToStringSidA( psid, &str );
        ok( r, "failed to convert sid\n" );
        if (r)
        {
            ok( !strcmp( str, refs[i].refStr ),
                "incorrect sid, expected %s, got %s\n", refs[i].refStr, str );
            LocalFree( str );
        }
        if( psid )
            FreeSid( psid );

        r = pConvertStringSidToSidA( refs[i].refStr, &psid );
        ok( r, "failed to parse sid string\n" );
        pisid = (PISID)psid;
        ok( pisid &&
         !memcmp( pisid->IdentifierAuthority.Value, refs[i].auth.Value,
         sizeof(refs[i].auth) ),
         "string sid %s didn't parse to expected value\n"
         "(got 0x%04x%08x, expected 0x%04x%08x)\n",
         refs[i].refStr,
         MAKEWORD( pisid->IdentifierAuthority.Value[1],
         pisid->IdentifierAuthority.Value[0] ),
         MAKELONG( MAKEWORD( pisid->IdentifierAuthority.Value[5],
         pisid->IdentifierAuthority.Value[4] ),
         MAKEWORD( pisid->IdentifierAuthority.Value[3],
         pisid->IdentifierAuthority.Value[2] ) ),
         MAKEWORD( refs[i].auth.Value[1], refs[i].auth.Value[0] ),
         MAKELONG( MAKEWORD( refs[i].auth.Value[5], refs[i].auth.Value[4] ),
         MAKEWORD( refs[i].auth.Value[3], refs[i].auth.Value[2] ) ) );
        if( psid )
            LocalFree( psid );
    }

    trace("String SIDs:\n");
    test_str_sid("AO");
    test_str_sid("RU");
    test_str_sid("AN");
    test_str_sid("AU");
    test_str_sid("BA");
    test_str_sid("BG");
    test_str_sid("BO");
    test_str_sid("BU");
    test_str_sid("CA");
    test_str_sid("CG");
    test_str_sid("CO");
    test_str_sid("DA");
    test_str_sid("DC");
    test_str_sid("DD");
    test_str_sid("DG");
    test_str_sid("DU");
    test_str_sid("EA");
    test_str_sid("ED");
    test_str_sid("WD");
    test_str_sid("PA");
    test_str_sid("IU");
    test_str_sid("LA");
    test_str_sid("LG");
    test_str_sid("LS");
    test_str_sid("SY");
    test_str_sid("NU");
    test_str_sid("NO");
    test_str_sid("NS");
    test_str_sid("PO");
    test_str_sid("PS");
    test_str_sid("PU");
    test_str_sid("RS");
    test_str_sid("RD");
    test_str_sid("RE");
    test_str_sid("RC");
    test_str_sid("SA");
    test_str_sid("SO");
    test_str_sid("SU");
}

static void test_trustee(void)
{
    GUID ObjectType = {0x12345678, 0x1234, 0x5678, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}};
    GUID InheritedObjectType = {0x23456789, 0x2345, 0x6786, {0x2, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99}};
    GUID ZeroGuid;
    OBJECTS_AND_NAME_ oan;
    OBJECTS_AND_SID oas;
    TRUSTEE trustee;
    PSID psid;
    char szObjectTypeName[] = "ObjectTypeName";
    char szInheritedObjectTypeName[] = "InheritedObjectTypeName";
    char szTrusteeName[] = "szTrusteeName";
    SID_IDENTIFIER_AUTHORITY auth = { {0x11,0x22,0,0,0, 0} };

    memset( &ZeroGuid, 0x00, sizeof (ZeroGuid) );

    pBuildTrusteeWithSidA = (fnBuildTrusteeWithSidA)
                    GetProcAddress( hmod, "BuildTrusteeWithSidA" );
    pBuildTrusteeWithNameA = (fnBuildTrusteeWithNameA)
                    GetProcAddress( hmod, "BuildTrusteeWithNameA" );
    pBuildTrusteeWithObjectsAndNameA = (fnBuildTrusteeWithObjectsAndNameA)
                    GetProcAddress (hmod, "BuildTrusteeWithObjectsAndNameA" );
    pBuildTrusteeWithObjectsAndSidA = (fnBuildTrusteeWithObjectsAndSidA)
                    GetProcAddress (hmod, "BuildTrusteeWithObjectsAndSidA" );
    pGetTrusteeNameA = (fnGetTrusteeNameA)
                    GetProcAddress (hmod, "GetTrusteeNameA" );
    if( !pBuildTrusteeWithSidA || !pBuildTrusteeWithNameA ||
        !pBuildTrusteeWithObjectsAndNameA || !pBuildTrusteeWithObjectsAndSidA ||
        !pGetTrusteeNameA )
        return;

    if ( ! AllocateAndInitializeSid( &auth, 1, 42, 0,0,0,0,0,0,0,&psid ) )
    {
        trace( "failed to init SID\n" );
       return;
    }

    /* test BuildTrusteeWithSidA */
    memset( &trustee, 0xff, sizeof trustee );
    pBuildTrusteeWithSidA( &trustee, psid );

    ok( trustee.pMultipleTrustee == NULL, "pMultipleTrustee wrong\n");
    ok( trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE, 
        "MultipleTrusteeOperation wrong\n");
    ok( trustee.TrusteeForm == TRUSTEE_IS_SID, "TrusteeForm wrong\n");
    ok( trustee.TrusteeType == TRUSTEE_IS_UNKNOWN, "TrusteeType wrong\n");
    ok( trustee.ptstrName == (LPSTR) psid, "ptstrName wrong\n" );

    /* test BuildTrusteeWithObjectsAndSidA (test 1) */
    memset( &trustee, 0xff, sizeof trustee );
    memset( &oas, 0xff, sizeof(oas) );
    pBuildTrusteeWithObjectsAndSidA(&trustee, &oas, &ObjectType,
                                    &InheritedObjectType, psid);

    ok(trustee.pMultipleTrustee == NULL, "pMultipleTrustee wrong\n");
    ok(trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE, "MultipleTrusteeOperation wrong\n");
    ok(trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_SID, "TrusteeForm wrong\n");
    ok(trustee.TrusteeType == TRUSTEE_IS_UNKNOWN, "TrusteeType wrong\n");
    ok(trustee.ptstrName == (LPSTR)&oas, "ptstrName wrong\n");
 
    ok(oas.ObjectsPresent == (ACE_OBJECT_TYPE_PRESENT | ACE_INHERITED_OBJECT_TYPE_PRESENT), "ObjectsPresent wrong\n");
    ok(!memcmp(&oas.ObjectTypeGuid, &ObjectType, sizeof(GUID)), "ObjectTypeGuid wrong\n");
    ok(!memcmp(&oas.InheritedObjectTypeGuid, &InheritedObjectType, sizeof(GUID)), "InheritedObjectTypeGuid wrong\n");
    ok(oas.pSid == psid, "pSid wrong\n");

    /* test GetTrusteeNameA */
    ok(pGetTrusteeNameA(&trustee) == (LPSTR)&oas, "GetTrusteeName returned wrong value\n");

    /* test BuildTrusteeWithObjectsAndSidA (test 2) */
    memset( &trustee, 0xff, sizeof trustee );
    memset( &oas, 0xff, sizeof(oas) );
    pBuildTrusteeWithObjectsAndSidA(&trustee, &oas, NULL,
                                    &InheritedObjectType, psid);

    ok(trustee.pMultipleTrustee == NULL, "pMultipleTrustee wrong\n");
    ok(trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE, "MultipleTrusteeOperation wrong\n");
    ok(trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_SID, "TrusteeForm wrong\n");
    ok(trustee.TrusteeType == TRUSTEE_IS_UNKNOWN, "TrusteeType wrong\n");
    ok(trustee.ptstrName == (LPSTR)&oas, "ptstrName wrong\n");
 
    ok(oas.ObjectsPresent == ACE_INHERITED_OBJECT_TYPE_PRESENT, "ObjectsPresent wrong\n");
    ok(!memcmp(&oas.ObjectTypeGuid, &ZeroGuid, sizeof(GUID)), "ObjectTypeGuid wrong\n");
    ok(!memcmp(&oas.InheritedObjectTypeGuid, &InheritedObjectType, sizeof(GUID)), "InheritedObjectTypeGuid wrong\n");
    ok(oas.pSid == psid, "pSid wrong\n");

    FreeSid( psid );

    /* test BuildTrusteeWithNameA */
    memset( &trustee, 0xff, sizeof trustee );
    pBuildTrusteeWithNameA( &trustee, szTrusteeName );

    ok( trustee.pMultipleTrustee == NULL, "pMultipleTrustee wrong\n");
    ok( trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE, 
        "MultipleTrusteeOperation wrong\n");
    ok( trustee.TrusteeForm == TRUSTEE_IS_NAME, "TrusteeForm wrong\n");
    ok( trustee.TrusteeType == TRUSTEE_IS_UNKNOWN, "TrusteeType wrong\n");
    ok( trustee.ptstrName == szTrusteeName, "ptstrName wrong\n" );

    /* test BuildTrusteeWithObjectsAndNameA (test 1) */
    memset( &trustee, 0xff, sizeof trustee );
    memset( &oan, 0xff, sizeof(oan) );
    pBuildTrusteeWithObjectsAndNameA(&trustee, &oan, SE_KERNEL_OBJECT, szObjectTypeName,
                                     szInheritedObjectTypeName, szTrusteeName);

    ok(trustee.pMultipleTrustee == NULL, "pMultipleTrustee wrong\n");
    ok(trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE, "MultipleTrusteeOperation wrong\n");
    ok(trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_NAME, "TrusteeForm wrong\n");
    ok(trustee.TrusteeType == TRUSTEE_IS_UNKNOWN, "TrusteeType wrong\n");
    ok(trustee.ptstrName == (LPTSTR)&oan, "ptstrName wrong\n");
 
    ok(oan.ObjectsPresent == (ACE_OBJECT_TYPE_PRESENT | ACE_INHERITED_OBJECT_TYPE_PRESENT), "ObjectsPresent wrong\n");
    ok(oan.ObjectType == SE_KERNEL_OBJECT, "ObjectType wrong\n");
    ok(oan.InheritedObjectTypeName == szInheritedObjectTypeName, "InheritedObjectTypeName wrong\n");
    ok(oan.ptstrName == szTrusteeName, "szTrusteeName wrong\n");

    /* test GetTrusteeNameA */
    ok(pGetTrusteeNameA(&trustee) == (LPSTR)&oan, "GetTrusteeName returned wrong value\n");

    /* test BuildTrusteeWithObjectsAndNameA (test 2) */
    memset( &trustee, 0xff, sizeof trustee );
    memset( &oan, 0xff, sizeof(oan) );
    pBuildTrusteeWithObjectsAndNameA(&trustee, &oan, SE_KERNEL_OBJECT, NULL,
                                     szInheritedObjectTypeName, szTrusteeName);

    ok(trustee.pMultipleTrustee == NULL, "pMultipleTrustee wrong\n");
    ok(trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE, "MultipleTrusteeOperation wrong\n");
    ok(trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_NAME, "TrusteeForm wrong\n");
    ok(trustee.TrusteeType == TRUSTEE_IS_UNKNOWN, "TrusteeType wrong\n");
    ok(trustee.ptstrName == (LPSTR)&oan, "ptstrName wrong\n");
 
    ok(oan.ObjectsPresent == ACE_INHERITED_OBJECT_TYPE_PRESENT, "ObjectsPresent wrong\n");
    ok(oan.ObjectType == SE_KERNEL_OBJECT, "ObjectType wrong\n");
    ok(oan.InheritedObjectTypeName == szInheritedObjectTypeName, "InheritedObjectTypeName wrong\n");
    ok(oan.ptstrName == szTrusteeName, "szTrusteeName wrong\n");

    /* test BuildTrusteeWithObjectsAndNameA (test 3) */
    memset( &trustee, 0xff, sizeof trustee );
    memset( &oan, 0xff, sizeof(oan) );
    pBuildTrusteeWithObjectsAndNameA(&trustee, &oan, SE_KERNEL_OBJECT, szObjectTypeName,
                                     NULL, szTrusteeName);

    ok(trustee.pMultipleTrustee == NULL, "pMultipleTrustee wrong\n");
    ok(trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE, "MultipleTrusteeOperation wrong\n");
    ok(trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_NAME, "TrusteeForm wrong\n");
    ok(trustee.TrusteeType == TRUSTEE_IS_UNKNOWN, "TrusteeType wrong\n");
    ok(trustee.ptstrName == (LPTSTR)&oan, "ptstrName wrong\n");
 
    ok(oan.ObjectsPresent == ACE_OBJECT_TYPE_PRESENT, "ObjectsPresent wrong\n");
    ok(oan.ObjectType == SE_KERNEL_OBJECT, "ObjectType wrong\n");
    ok(oan.InheritedObjectTypeName == NULL, "InheritedObjectTypeName wrong\n");
    ok(oan.ptstrName == szTrusteeName, "szTrusteeName wrong\n");
}
 
/* If the first isn't defined, assume none is */
#ifndef SE_MIN_WELL_KNOWN_PRIVILEGE
#define SE_MIN_WELL_KNOWN_PRIVILEGE       2L
#define SE_CREATE_TOKEN_PRIVILEGE         2L
#define SE_ASSIGNPRIMARYTOKEN_PRIVILEGE   3L
#define SE_LOCK_MEMORY_PRIVILEGE          4L
#define SE_INCREASE_QUOTA_PRIVILEGE       5L
#define SE_MACHINE_ACCOUNT_PRIVILEGE      6L
#define SE_TCB_PRIVILEGE                  7L
#define SE_SECURITY_PRIVILEGE             8L
#define SE_TAKE_OWNERSHIP_PRIVILEGE       9L
#define SE_LOAD_DRIVER_PRIVILEGE         10L
#define SE_SYSTEM_PROFILE_PRIVILEGE      11L
#define SE_SYSTEMTIME_PRIVILEGE          12L
#define SE_PROF_SINGLE_PROCESS_PRIVILEGE 13L
#define SE_INC_BASE_PRIORITY_PRIVILEGE   14L
#define SE_CREATE_PAGEFILE_PRIVILEGE     15L
#define SE_CREATE_PERMANENT_PRIVILEGE    16L
#define SE_BACKUP_PRIVILEGE              17L
#define SE_RESTORE_PRIVILEGE             18L
#define SE_SHUTDOWN_PRIVILEGE            19L
#define SE_DEBUG_PRIVILEGE               20L
#define SE_AUDIT_PRIVILEGE               21L
#define SE_SYSTEM_ENVIRONMENT_PRIVILEGE  22L
#define SE_CHANGE_NOTIFY_PRIVILLEGE      23L
#define SE_REMOTE_SHUTDOWN_PRIVILEGE     24L
#define SE_UNDOCK_PRIVILEGE              25L
#define SE_SYNC_AGENT_PRIVILEGE          26L
#define SE_ENABLE_DELEGATION_PRIVILEGE   27L
#define SE_MANAGE_VOLUME_PRIVILEGE       28L
#define SE_IMPERSONATE_PRIVILEGE         29L
#define SE_CREATE_GLOBAL_PRIVILEGE       30L
#define SE_MAX_WELL_KNOWN_PRIVILEGE      SE_CREATE_GLOBAL_PRIVILEGE
#endif /* ndef SE_MIN_WELL_KNOWN_PRIVILEGE */

static void test_allocateLuid(void)
{
    BOOL (WINAPI *pAllocateLocallyUniqueId)(PLUID);
    LUID luid1, luid2;
    BOOL ret;

    pAllocateLocallyUniqueId = (void*)GetProcAddress(hmod, "AllocateLocallyUniqueId");
    if (!pAllocateLocallyUniqueId) return;

    ret = pAllocateLocallyUniqueId(&luid1);
    if (!ret && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
        return;

    ok(ret,
     "AllocateLocallyUniqueId failed: %d\n", GetLastError());
    ret = pAllocateLocallyUniqueId(&luid2);
    ok( ret,
     "AllocateLocallyUniqueId failed: %d\n", GetLastError());
    ok(luid1.LowPart > SE_MAX_WELL_KNOWN_PRIVILEGE || luid1.HighPart != 0,
     "AllocateLocallyUniqueId returned a well-known LUID\n");
    ok(luid1.LowPart != luid2.LowPart || luid1.HighPart != luid2.HighPart,
     "AllocateLocallyUniqueId returned non-unique LUIDs\n");
    ret = pAllocateLocallyUniqueId(NULL);
    ok( !ret && GetLastError() == ERROR_NOACCESS,
     "AllocateLocallyUniqueId(NULL) didn't return ERROR_NOACCESS: %d\n",
     GetLastError());
}

static void test_lookupPrivilegeName(void)
{
    BOOL (WINAPI *pLookupPrivilegeNameA)(LPCSTR, PLUID, LPSTR, LPDWORD);
    char buf[MAX_PATH]; /* arbitrary, seems long enough */
    DWORD cchName = sizeof(buf);
    LUID luid = { 0, 0 };
    LONG i;
    BOOL ret;

    /* check whether it's available first */
    pLookupPrivilegeNameA = (void*)GetProcAddress(hmod, "LookupPrivilegeNameA");
    if (!pLookupPrivilegeNameA) return;
    luid.LowPart = SE_CREATE_TOKEN_PRIVILEGE;
    ret = pLookupPrivilegeNameA(NULL, &luid, buf, &cchName);
    if (!ret && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
        return;

    /* check with a short buffer */
    cchName = 0;
    luid.LowPart = SE_CREATE_TOKEN_PRIVILEGE;
    ret = pLookupPrivilegeNameA(NULL, &luid, NULL, &cchName);
    ok( !ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
     "LookupPrivilegeNameA didn't fail with ERROR_INSUFFICIENT_BUFFER: %d\n",
     GetLastError());
    ok(cchName == strlen("SeCreateTokenPrivilege") + 1,
     "LookupPrivilegeNameA returned an incorrect required length for\n"
     "SeCreateTokenPrivilege (got %d, expected %d)\n", cchName,
     lstrlenA("SeCreateTokenPrivilege") + 1);
    /* check a known value and its returned length on success */
    cchName = sizeof(buf);
    ok(pLookupPrivilegeNameA(NULL, &luid, buf, &cchName) &&
     cchName == strlen("SeCreateTokenPrivilege"),
     "LookupPrivilegeNameA returned an incorrect output length for\n"
     "SeCreateTokenPrivilege (got %d, expected %d)\n", cchName,
     (int)strlen("SeCreateTokenPrivilege"));
    /* check known values */
    for (i = SE_MIN_WELL_KNOWN_PRIVILEGE; i < SE_MAX_WELL_KNOWN_PRIVILEGE; i++)
    {
        luid.LowPart = i;
        cchName = sizeof(buf);
        ret = pLookupPrivilegeNameA(NULL, &luid, buf, &cchName);
        ok( ret || GetLastError() == ERROR_NO_SUCH_PRIVILEGE,
         "LookupPrivilegeNameA(0.%d) failed: %d\n", i, GetLastError());
    }
    /* check a bogus LUID */
    luid.LowPart = 0xdeadbeef;
    cchName = sizeof(buf);
    ret = pLookupPrivilegeNameA(NULL, &luid, buf, &cchName);
    ok( !ret && GetLastError() == ERROR_NO_SUCH_PRIVILEGE,
     "LookupPrivilegeNameA didn't fail with ERROR_NO_SUCH_PRIVILEGE: %d\n",
     GetLastError());
    /* check on a bogus system */
    luid.LowPart = SE_CREATE_TOKEN_PRIVILEGE;
    cchName = sizeof(buf);
    ret = pLookupPrivilegeNameA("b0gu5.Nam3", &luid, buf, &cchName);
    ok( !ret && GetLastError() == RPC_S_SERVER_UNAVAILABLE,
     "LookupPrivilegeNameA didn't fail with RPC_S_SERVER_UNAVAILABLE: %d\n",
     GetLastError());
}

struct NameToLUID
{
    const char *name;
    DWORD lowPart;
};

static void test_lookupPrivilegeValue(void)
{
    static const struct NameToLUID privs[] = {
     { "SeCreateTokenPrivilege", SE_CREATE_TOKEN_PRIVILEGE },
     { "SeAssignPrimaryTokenPrivilege", SE_ASSIGNPRIMARYTOKEN_PRIVILEGE },
     { "SeLockMemoryPrivilege", SE_LOCK_MEMORY_PRIVILEGE },
     { "SeIncreaseQuotaPrivilege", SE_INCREASE_QUOTA_PRIVILEGE },
     { "SeMachineAccountPrivilege", SE_MACHINE_ACCOUNT_PRIVILEGE },
     { "SeTcbPrivilege", SE_TCB_PRIVILEGE },
     { "SeSecurityPrivilege", SE_SECURITY_PRIVILEGE },
     { "SeTakeOwnershipPrivilege", SE_TAKE_OWNERSHIP_PRIVILEGE },
     { "SeLoadDriverPrivilege", SE_LOAD_DRIVER_PRIVILEGE },
     { "SeSystemProfilePrivilege", SE_SYSTEM_PROFILE_PRIVILEGE },
     { "SeSystemtimePrivilege", SE_SYSTEMTIME_PRIVILEGE },
     { "SeProfileSingleProcessPrivilege", SE_PROF_SINGLE_PROCESS_PRIVILEGE },
     { "SeIncreaseBasePriorityPrivilege", SE_INC_BASE_PRIORITY_PRIVILEGE },
     { "SeCreatePagefilePrivilege", SE_CREATE_PAGEFILE_PRIVILEGE },
     { "SeCreatePermanentPrivilege", SE_CREATE_PERMANENT_PRIVILEGE },
     { "SeBackupPrivilege", SE_BACKUP_PRIVILEGE },
     { "SeRestorePrivilege", SE_RESTORE_PRIVILEGE },
     { "SeShutdownPrivilege", SE_SHUTDOWN_PRIVILEGE },
     { "SeDebugPrivilege", SE_DEBUG_PRIVILEGE },
     { "SeAuditPrivilege", SE_AUDIT_PRIVILEGE },
     { "SeSystemEnvironmentPrivilege", SE_SYSTEM_ENVIRONMENT_PRIVILEGE },
     { "SeChangeNotifyPrivilege", SE_CHANGE_NOTIFY_PRIVILLEGE },
     { "SeRemoteShutdownPrivilege", SE_REMOTE_SHUTDOWN_PRIVILEGE },
     { "SeUndockPrivilege", SE_UNDOCK_PRIVILEGE },
     { "SeSyncAgentPrivilege", SE_SYNC_AGENT_PRIVILEGE },
     { "SeEnableDelegationPrivilege", SE_ENABLE_DELEGATION_PRIVILEGE },
     { "SeManageVolumePrivilege", SE_MANAGE_VOLUME_PRIVILEGE },
     { "SeImpersonatePrivilege", SE_IMPERSONATE_PRIVILEGE },
     { "SeCreateGlobalPrivilege", SE_CREATE_GLOBAL_PRIVILEGE },
    };
    BOOL (WINAPI *pLookupPrivilegeValueA)(LPCSTR, LPCSTR, PLUID);
    int i;
    LUID luid;
    BOOL ret;

    /* check whether it's available first */
    pLookupPrivilegeValueA = (void*)GetProcAddress(hmod, "LookupPrivilegeValueA");
    if (!pLookupPrivilegeValueA) return;
    ret = pLookupPrivilegeValueA(NULL, "SeCreateTokenPrivilege", &luid);
    if (!ret && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
        return;

    /* check a bogus system name */
    ret = pLookupPrivilegeValueA("b0gu5.Nam3", "SeCreateTokenPrivilege", &luid);
    ok( !ret && GetLastError() == RPC_S_SERVER_UNAVAILABLE,
     "LookupPrivilegeValueA didn't fail with RPC_S_SERVER_UNAVAILABLE: %d\n",
     GetLastError());
    /* check a NULL string */
    ret = pLookupPrivilegeValueA(NULL, 0, &luid);
    ok( !ret && GetLastError() == ERROR_NO_SUCH_PRIVILEGE,
     "LookupPrivilegeValueA didn't fail with ERROR_NO_SUCH_PRIVILEGE: %d\n",
     GetLastError());
    /* check a bogus privilege name */
    ret = pLookupPrivilegeValueA(NULL, "SeBogusPrivilege", &luid);
    ok( !ret && GetLastError() == ERROR_NO_SUCH_PRIVILEGE,
     "LookupPrivilegeValueA didn't fail with ERROR_NO_SUCH_PRIVILEGE: %d\n",
     GetLastError());
    /* check case insensitive */
    ret = pLookupPrivilegeValueA(NULL, "sEcREATEtOKENpRIVILEGE", &luid);
    ok( ret,
     "LookupPrivilegeValueA(NULL, sEcREATEtOKENpRIVILEGE, &luid) failed: %d\n",
     GetLastError());
    for (i = 0; i < sizeof(privs) / sizeof(privs[0]); i++)
    {
        /* Not all privileges are implemented on all Windows versions, so
         * don't worry if the call fails
         */
        if (pLookupPrivilegeValueA(NULL, privs[i].name, &luid))
        {
            ok(luid.LowPart == privs[i].lowPart,
             "LookupPrivilegeValueA returned an invalid LUID for %s\n",
             privs[i].name);
        }
    }
}

static void test_luid(void)
{
    test_allocateLuid();
    test_lookupPrivilegeName();
    test_lookupPrivilegeValue();
}

static void test_FileSecurity(void)
{
    char directory[MAX_PATH];
    DWORD retval, outSize;
    BOOL result;
    BYTE buffer[0x40];

    pGetFileSecurityA = (fnGetFileSecurityA)
                    GetProcAddress( hmod, "GetFileSecurityA" );
    if( !pGetFileSecurityA )
        return;

    retval = GetTempPathA(sizeof(directory), directory);
    if (!retval) {
        trace("GetTempPathA failed\n");
        return;
    }

    strcpy(directory, "\\Should not exist");

    SetLastError(NO_ERROR);
    result = pGetFileSecurityA( directory,OWNER_SECURITY_INFORMATION,buffer,0x40,&outSize);
    ok(!result, "GetFileSecurityA should fail for not existing directories/files\n"); 
    ok( (GetLastError() == ERROR_FILE_NOT_FOUND ) ||
        (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED) , 
        "last error ERROR_FILE_NOT_FOUND / ERROR_CALL_NOT_IMPLEMENTED (98) "
        "expected, got %d\n", GetLastError());
}

static void test_AccessCheck(void)
{
    PSID EveryoneSid = NULL, AdminSid = NULL, UsersSid = NULL;
    PACL Acl = NULL;
    SECURITY_DESCRIPTOR *SecurityDescriptor = NULL;
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = { SECURITY_WORLD_SID_AUTHORITY };
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = { SECURITY_NT_AUTHORITY };
    GENERIC_MAPPING Mapping = { KEY_READ, KEY_WRITE, KEY_EXECUTE, KEY_ALL_ACCESS };
    ACCESS_MASK Access;
    BOOL AccessStatus;
    HANDLE Token;
    HANDLE ProcessToken;
    BOOL ret;
    DWORD PrivSetLen;
    PRIVILEGE_SET *PrivSet;
    BOOL res;
    HMODULE NtDllModule;
    BOOLEAN Enabled;
    DWORD err;

    NtDllModule = GetModuleHandle("ntdll.dll");

    if (!NtDllModule)
    {
        trace("not running on NT, skipping test\n");
        return;
    }
    pRtlAdjustPrivilege = (fnRtlAdjustPrivilege)
                          GetProcAddress(NtDllModule, "RtlAdjustPrivilege");
    if (!pRtlAdjustPrivilege) return;

    Acl = HeapAlloc(GetProcessHeap(), 0, 256);
    res = InitializeAcl(Acl, 256, ACL_REVISION);
    if(!res && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        skip("ACLs not implemented - skipping tests\n");
        return;
    }
    ok(res, "InitializeAcl failed with error %d\n", GetLastError());

    res = AllocateAndInitializeSid( &SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &EveryoneSid);
    ok(res, "AllocateAndInitializeSid failed with error %d\n", GetLastError());

    res = AllocateAndInitializeSid( &SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &AdminSid);
    ok(res, "AllocateAndInitializeSid failed with error %d\n", GetLastError());

    res = AllocateAndInitializeSid( &SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &UsersSid);
    ok(res, "AllocateAndInitializeSid failed with error %d\n", GetLastError());

    res = AddAccessAllowedAce(Acl, ACL_REVISION, KEY_READ, EveryoneSid);
    ok(res, "AddAccessAllowedAceEx failed with error %d\n", GetLastError());

    res = AddAccessDeniedAce(Acl, ACL_REVISION, KEY_SET_VALUE, AdminSid);
    ok(res, "AddAccessDeniedAce failed with error %d\n", GetLastError());

    SecurityDescriptor = HeapAlloc(GetProcessHeap(), 0, SECURITY_DESCRIPTOR_MIN_LENGTH);

    res = InitializeSecurityDescriptor(SecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    ok(res, "InitializeSecurityDescriptor failed with error %d\n", GetLastError());

    res = SetSecurityDescriptorDacl(SecurityDescriptor, TRUE, Acl, FALSE);
    ok(res, "SetSecurityDescriptorDacl failed with error %d\n", GetLastError());

    PrivSetLen = FIELD_OFFSET(PRIVILEGE_SET, Privilege[16]);
    PrivSet = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, PrivSetLen);
    PrivSet->PrivilegeCount = 16;

    res = OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE|TOKEN_QUERY, &ProcessToken);
    ok(res, "OpenProcessToken failed with error %d\n", GetLastError());

    pRtlAdjustPrivilege(SE_SECURITY_PRIVILEGE, FALSE, TRUE, &Enabled);

    res = DuplicateToken(ProcessToken, SecurityIdentification, &Token);
    ok(res, "DuplicateToken failed with error %d\n", GetLastError());

    /* SD without owner/group */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0xdeadbeef;
    ret = AccessCheck(SecurityDescriptor, Token, KEY_QUERY_VALUE, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_INVALID_SECURITY_DESCR, "AccessCheck should have "
       "failed with ERROR_INVALID_SECURITY_DESCR, instead of %d\n", err);
    ok(Access == 0xdeadbeef && AccessStatus == 0xdeadbeef,
       "Access and/or AccessStatus were changed!\n");

    /* Set owner and group */
    res = SetSecurityDescriptorOwner(SecurityDescriptor, AdminSid, FALSE);
    ok(res, "SetSecurityDescriptorOwner failed with error %d\n", GetLastError());
    res = SetSecurityDescriptorGroup(SecurityDescriptor, UsersSid, TRUE);
    ok(res, "SetSecurityDescriptorGroup failed with error %d\n", GetLastError());

    /* Generic access mask */
    SetLastError(0xdeadbeef);
    ret = AccessCheck(SecurityDescriptor, Token, GENERIC_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_GENERIC_NOT_MAPPED, "AccessCheck should have failed "
       "with ERROR_GENERIC_NOT_MAPPED, instead of %d\n", err);
    ok(Access == 0xdeadbeef && AccessStatus == 0xdeadbeef,
       "Access and/or AccessStatus were changed!\n");

    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    ok(ret, "AccessCheck failed with error %d\n", GetLastError());
    ok(AccessStatus && (Access == KEY_READ),
        "AccessCheck failed to grant access with error %d\n",
        GetLastError());

    ret = AccessCheck(SecurityDescriptor, Token, MAXIMUM_ALLOWED, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    ok(ret, "AccessCheck failed with error %d\n", GetLastError());
    ok(AccessStatus,
        "AccessCheck failed to grant any access with error %d\n",
        GetLastError());
    trace("AccessCheck with MAXIMUM_ALLOWED got Access 0x%08x\n", Access);

    /* Access denied by SD */
    SetLastError(0xdeadbeef);
    ret = AccessCheck(SecurityDescriptor, Token, KEY_WRITE, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    ok(ret, "AccessCheck failed with error %d\n", GetLastError());
    err = GetLastError();
    ok(!AccessStatus && err == ERROR_ACCESS_DENIED, "AccessCheck should have failed "
       "with ERROR_ACCESS_DENIED, instead of %d\n", err);
    ok(!Access, "Should have failed to grant any access, got 0x%08x\n", Access);

    SetLastError(0);
    PrivSet->PrivilegeCount = 16;
    ret = AccessCheck(SecurityDescriptor, Token, ACCESS_SYSTEM_SECURITY, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    ok(ret && !AccessStatus && GetLastError() == ERROR_PRIVILEGE_NOT_HELD,
        "AccessCheck should have failed with ERROR_PRIVILEGE_NOT_HELD, instead of %d\n",
        GetLastError());

    ret = pRtlAdjustPrivilege(SE_SECURITY_PRIVILEGE, TRUE, TRUE, &Enabled);
    if (!ret)
    {
        SetLastError(0);
        PrivSet->PrivilegeCount = 16;
        ret = AccessCheck(SecurityDescriptor, Token, ACCESS_SYSTEM_SECURITY, &Mapping,
                          PrivSet, &PrivSetLen, &Access, &AccessStatus);
        ok(ret && AccessStatus && GetLastError() == 0,
            "AccessCheck should have succeeded, error %d\n",
            GetLastError());
        ok(Access == ACCESS_SYSTEM_SECURITY,
            "Access should be equal to ACCESS_SYSTEM_SECURITY instead of 0x%08x\n",
            Access);
    }
    else
        trace("Couldn't get SE_SECURITY_PRIVILEGE (0x%08x), skipping ACCESS_SYSTEM_SECURITY test\n",
            ret);

    CloseHandle(Token);

    res = DuplicateToken(ProcessToken, SecurityAnonymous, &Token);
    ok(res, "DuplicateToken failed with error %d\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = AccessCheck(SecurityDescriptor, Token, MAXIMUM_ALLOWED, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_BAD_IMPERSONATION_LEVEL, "AccessCheck should have failed "
       "with ERROR_BAD_IMPERSONATION_LEVEL, instead of %d\n", err);

    CloseHandle(Token);

    SetLastError(0xdeadbeef);
    ret = AccessCheck(SecurityDescriptor, ProcessToken, KEY_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_NO_IMPERSONATION_TOKEN, "AccessCheck should have failed "
       "with ERROR_NO_IMPERSONATION_TOKEN, instead of %d\n", err);

    CloseHandle(ProcessToken);

    if (EveryoneSid)
        FreeSid(EveryoneSid);
    if (AdminSid)
        FreeSid(AdminSid);
    if (UsersSid)
        FreeSid(UsersSid);
    HeapFree(GetProcessHeap(), 0, Acl);
    HeapFree(GetProcessHeap(), 0, SecurityDescriptor);
    HeapFree(GetProcessHeap(), 0, PrivSet);
}

/* test GetTokenInformation for the various attributes */
static void test_token_attr(void)
{
    HANDLE Token, ImpersonationToken;
    DWORD Size;
    TOKEN_PRIVILEGES *Privileges;
    TOKEN_GROUPS *Groups;
    TOKEN_USER *User;
    BOOL ret;
    DWORD i, GLE;
    LPSTR SidString;
    SECURITY_IMPERSONATION_LEVEL ImpersonationLevel;

    /* cygwin-like use case */
    ret = OpenProcessToken(GetCurrentProcess(), MAXIMUM_ALLOWED, &Token);
    ok(ret, "OpenProcessToken failed with error %d\n", GetLastError());
    if (ret)
    {
        BYTE buf[1024];
        Size = sizeof(buf);
        ret = GetTokenInformation(Token, TokenUser,(void*)buf, Size, &Size);
        ok(ret, "GetTokenInformation failed with error %d\n", GetLastError());
        Size = sizeof(ImpersonationLevel);
        ret = GetTokenInformation(Token, TokenImpersonationLevel, &ImpersonationLevel, Size, &Size);
        GLE = GetLastError();
        ok(!ret && (GLE == ERROR_INVALID_PARAMETER), "GetTokenInformation(TokenImpersonationLevel) on primary token should have failed with ERROR_INVALID_PARAMETER instead of %d\n", GLE);
        CloseHandle(Token);
    }

    if(!pConvertSidToStringSidA)
        return;

    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY|TOKEN_DUPLICATE, &Token);
    GLE = GetLastError();
    ok(ret || (GLE == ERROR_CALL_NOT_IMPLEMENTED), 
        "OpenProcessToken failed with error %d\n", GLE);
    if(!ret && (GLE == ERROR_CALL_NOT_IMPLEMENTED))
    {
        trace("OpenProcessToken() not implemented, skipping test_token_attr()\n");
        return;
    }

    /* groups */
    ret = GetTokenInformation(Token, TokenGroups, NULL, 0, &Size);
    Groups = HeapAlloc(GetProcessHeap(), 0, Size);
    ret = GetTokenInformation(Token, TokenGroups, Groups, Size, &Size);
    ok(ret, "GetTokenInformation(TokenGroups) failed with error %d\n", GetLastError());
    trace("TokenGroups:\n");
    for (i = 0; i < Groups->GroupCount; i++)
    {
        DWORD NameLength = 255;
        TCHAR Name[255];
        DWORD DomainLength = 255;
        TCHAR Domain[255];
        SID_NAME_USE SidNameUse;
        pConvertSidToStringSidA(Groups->Groups[i].Sid, &SidString);
        Name[0] = '\0';
        Domain[0] = '\0';
        ret = LookupAccountSid(NULL, Groups->Groups[i].Sid, Name, &NameLength, Domain, &DomainLength, &SidNameUse);
        ok(ret, "LookupAccountSid(%s) failed with error %d\n", SidString, GetLastError());
        trace("\t%s, %s\\%s use: %d attr: 0x%08x\n", SidString, Domain, Name, SidNameUse, Groups->Groups[i].Attributes);
        LocalFree(SidString);
    }
    HeapFree(GetProcessHeap(), 0, Groups);

    /* user */
    ret = GetTokenInformation(Token, TokenUser, NULL, 0, &Size);
    ok(!ret && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
        "GetTokenInformation(TokenUser) failed with error %d\n", GetLastError());
    User = HeapAlloc(GetProcessHeap(), 0, Size);
    ret = GetTokenInformation(Token, TokenUser, User, Size, &Size);
    ok(ret,
        "GetTokenInformation(TokenUser) failed with error %d\n", GetLastError());

    pConvertSidToStringSidA(User->User.Sid, &SidString);
    trace("TokenUser: %s attr: 0x%08x\n", SidString, User->User.Attributes);
    LocalFree(SidString);
    HeapFree(GetProcessHeap(), 0, User);

    /* privileges */
    ret = GetTokenInformation(Token, TokenPrivileges, NULL, 0, &Size);
    ok(!ret && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
        "GetTokenInformation(TokenPrivileges) failed with error %d\n", GetLastError());
    Privileges = HeapAlloc(GetProcessHeap(), 0, Size);
    ret = GetTokenInformation(Token, TokenPrivileges, Privileges, Size, &Size);
    ok(ret,
        "GetTokenInformation(TokenPrivileges) failed with error %d\n", GetLastError());
    trace("TokenPrivileges:\n");
    for (i = 0; i < Privileges->PrivilegeCount; i++)
    {
        TCHAR Name[256];
        DWORD NameLen = sizeof(Name)/sizeof(Name[0]);
        LookupPrivilegeName(NULL, &Privileges->Privileges[i].Luid, Name, &NameLen);
        trace("\t%s, 0x%x\n", Name, Privileges->Privileges[i].Attributes);
    }
    HeapFree(GetProcessHeap(), 0, Privileges);

    ret = DuplicateToken(Token, SecurityAnonymous, &ImpersonationToken);
    ok(ret, "DuplicateToken failed with error %d\n", GetLastError());

    Size = sizeof(ImpersonationLevel);
    ret = GetTokenInformation(ImpersonationToken, TokenImpersonationLevel, &ImpersonationLevel, Size, &Size);
    ok(ret, "GetTokenInformation(TokenImpersonationLevel) failed with error %d\n", GetLastError());
    ok(ImpersonationLevel == SecurityAnonymous, "ImpersonationLevel should have been SecurityAnonymous instead of %d\n", ImpersonationLevel);

    CloseHandle(ImpersonationToken);
    CloseHandle(Token);
}

typedef union _MAX_SID
{
    SID sid;
    char max[SECURITY_MAX_SID_SIZE];
} MAX_SID;

static void test_sid_str(PSID * sid)
{
    char *str_sid;
    BOOL ret = pConvertSidToStringSidA(sid, &str_sid);
    ok(ret, "ConvertSidToStringSidA() failed: %d\n", GetLastError());
    if (ret)
    {
        char account[MAX_PATH], domain[MAX_PATH];
        SID_NAME_USE use;
        DWORD acc_size = MAX_PATH;
        DWORD dom_size = MAX_PATH;
        ret = LookupAccountSid(NULL, sid, account, &acc_size, domain, &dom_size, &use);
        ok(ret || (!ret && (GetLastError() == ERROR_NONE_MAPPED)),
           "LookupAccountSid(%s) failed: %d\n", str_sid, GetLastError());
        if (ret)
            trace(" %s %s\\%s %d\n", str_sid, domain, account, use);
        else if (GetLastError() == ERROR_NONE_MAPPED)
            trace(" %s couldn't be mapped\n", str_sid);
        LocalFree(str_sid);
    }
}

static void test_LookupAccountSid(void)
{
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = { SECURITY_NT_AUTHORITY };
    CHAR accountA[MAX_PATH], domainA[MAX_PATH];
    DWORD acc_sizeA, dom_sizeA;
    DWORD real_acc_sizeA, real_dom_sizeA;
    WCHAR accountW[MAX_PATH], domainW[MAX_PATH];
    DWORD acc_sizeW, dom_sizeW;
    DWORD real_acc_sizeW, real_dom_sizeW;
    PSID pUsersSid = NULL;
    SID_NAME_USE use;
    BOOL ret;
    DWORD size;
    MAX_SID  max_sid;
    CHAR *str_sidA;
    int i;

    /* native windows crashes if account size, domain size, or name use is NULL */

    ret = AllocateAndInitializeSid(&SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &pUsersSid);
    ok(ret || (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED),
       "AllocateAndInitializeSid failed with error %d\n", GetLastError());

    /* not running on NT so give up */
    if (!ret && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED))
        return;

    real_acc_sizeA = MAX_PATH;
    real_dom_sizeA = MAX_PATH;
    ret = LookupAccountSidA(NULL, pUsersSid, accountA, &real_acc_sizeA, domainA, &real_dom_sizeA, &use);
    ok(ret, "LookupAccountSidA() Expected TRUE, got FALSE\n");

    /* try NULL account */
    acc_sizeA = MAX_PATH;
    dom_sizeA = MAX_PATH;
    ret = LookupAccountSidA(NULL, pUsersSid, NULL, &acc_sizeA, domainA, &dom_sizeA, &use);
    ok(ret, "LookupAccountSidA() Expected TRUE, got FALSE\n");

    /* try NULL domain */
    acc_sizeA = MAX_PATH;
    dom_sizeA = MAX_PATH;
    ret = LookupAccountSidA(NULL, pUsersSid, accountA, &acc_sizeA, NULL, &dom_sizeA, &use);
    ok(ret, "LookupAccountSidA() Expected TRUE, got FALSE\n");

    /* try a small account buffer */
    acc_sizeA = 1;
    dom_sizeA = MAX_PATH;
    accountA[0] = 0;
    ret = LookupAccountSidA(NULL, pUsersSid, accountA, &acc_sizeA, domainA, &dom_sizeA, &use);
    ok(!ret, "LookupAccountSidA() Expected FALSE got TRUE\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "LookupAccountSidA() Expected ERROR_NOT_ENOUGH_MEMORY, got %u\n", GetLastError());

    /* try a 0 sized account buffer */
    acc_sizeA = 0;
    dom_sizeA = MAX_PATH;
    accountA[0] = 0;
    ret = LookupAccountSidA(NULL, pUsersSid, accountA, &acc_sizeA, domainA, &dom_sizeA, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(acc_sizeA == real_acc_sizeA + 1,
       "LookupAccountSidA() Expected acc_size = %u, got %u\n",
       real_acc_sizeA + 1, acc_sizeA);

    /* try a 0 sized account buffer */
    acc_sizeA = 0;
    dom_sizeA = MAX_PATH;
    ret = LookupAccountSidA(NULL, pUsersSid, NULL, &acc_sizeA, domainA, &dom_sizeA, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(acc_sizeA == real_acc_sizeA + 1,
       "LookupAccountSid() Expected acc_size = %u, got %u\n",
       real_acc_sizeA + 1, acc_sizeA);

    /* try a small domain buffer */
    dom_sizeA = 1;
    acc_sizeA = MAX_PATH;
    accountA[0] = 0;
    ret = LookupAccountSidA(NULL, pUsersSid, accountA, &acc_sizeA, domainA, &dom_sizeA, &use);
    ok(!ret, "LookupAccountSidA() Expected FALSE got TRUE\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "LookupAccountSidA() Expected ERROR_NOT_ENOUGH_MEMORY, got %u\n", GetLastError());

    /* try a 0 sized domain buffer */
    dom_sizeA = 0;
    acc_sizeA = MAX_PATH;
    accountA[0] = 0;
    ret = LookupAccountSidA(NULL, pUsersSid, accountA, &acc_sizeA, domainA, &dom_sizeA, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(dom_sizeA == real_dom_sizeA + 1,
       "LookupAccountSidA() Expected dom_size = %u, got %u\n",
       real_dom_sizeA + 1, dom_sizeA);

    /* try a 0 sized domain buffer */
    dom_sizeA = 0;
    acc_sizeA = MAX_PATH;
    ret = LookupAccountSidA(NULL, pUsersSid, accountA, &acc_sizeA, NULL, &dom_sizeA, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(dom_sizeA == real_dom_sizeA + 1,
       "LookupAccountSidA() Expected dom_size = %u, got %u\n",
       real_dom_sizeA + 1, dom_sizeA);

    real_acc_sizeW = MAX_PATH;
    real_dom_sizeW = MAX_PATH;
    ret = LookupAccountSidW(NULL, pUsersSid, accountW, &real_acc_sizeW, domainW, &real_dom_sizeW, &use);
    ok(ret, "LookupAccountSidW() Expected TRUE, got FALSE\n");

    /* native windows crashes if domainW or accountW is NULL */

    /* try a small account buffer */
    acc_sizeW = 1;
    dom_sizeW = MAX_PATH;
    accountW[0] = 0;
    ret = LookupAccountSidW(NULL, pUsersSid, accountW, &acc_sizeW, domainW, &dom_sizeW, &use);
    ok(!ret, "LookupAccountSidW() Expected FALSE got TRUE\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "LookupAccountSidW() Expected ERROR_NOT_ENOUGH_MEMORY, got %u\n", GetLastError());

    /* try a 0 sized account buffer */
    acc_sizeW = 0;
    dom_sizeW = MAX_PATH;
    accountW[0] = 0;
    ret = LookupAccountSidW(NULL, pUsersSid, accountW, &acc_sizeW, domainW, &dom_sizeW, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(acc_sizeW == real_acc_sizeW + 1,
       "LookupAccountSidW() Expected acc_size = %u, got %u\n",
       real_acc_sizeW + 1, acc_sizeW);

    /* try a 0 sized account buffer */
    acc_sizeW = 0;
    dom_sizeW = MAX_PATH;
    ret = LookupAccountSidW(NULL, pUsersSid, NULL, &acc_sizeW, domainW, &dom_sizeW, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(acc_sizeW == real_acc_sizeW + 1,
       "LookupAccountSidW() Expected acc_size = %u, got %u\n",
       real_acc_sizeW + 1, acc_sizeW);

    /* try a small domain buffer */
    dom_sizeW = 1;
    acc_sizeW = MAX_PATH;
    accountW[0] = 0;
    ret = LookupAccountSidW(NULL, pUsersSid, accountW, &acc_sizeW, domainW, &dom_sizeW, &use);
    ok(!ret, "LookupAccountSidW() Expected FALSE got TRUE\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "LookupAccountSidW() Expected ERROR_NOT_ENOUGH_MEMORY, got %u\n", GetLastError());

    /* try a 0 sized domain buffer */
    dom_sizeW = 0;
    acc_sizeW = MAX_PATH;
    accountW[0] = 0;
    ret = LookupAccountSidW(NULL, pUsersSid, accountW, &acc_sizeW, domainW, &dom_sizeW, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(dom_sizeW == real_dom_sizeW + 1,
       "LookupAccountSidW() Expected dom_size = %u, got %u\n",
       real_dom_sizeW + 1, dom_sizeW);

    /* try a 0 sized domain buffer */
    dom_sizeW = 0;
    acc_sizeW = MAX_PATH;
    ret = LookupAccountSidW(NULL, pUsersSid, accountW, &acc_sizeW, NULL, &dom_sizeW, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(dom_sizeW == real_dom_sizeW + 1,
       "LookupAccountSidW() Expected dom_size = %u, got %u\n",
       real_dom_sizeW + 1, dom_sizeW);

    FreeSid(pUsersSid);

    pCreateWellKnownSid = (fnCreateWellKnownSid)GetProcAddress( hmod, "CreateWellKnownSid" );

    if (pCreateWellKnownSid && pConvertSidToStringSidA)
    {
        trace("Well Known SIDs:\n");
        for (i = 0; i <= 60; i++)
        {
            size = SECURITY_MAX_SID_SIZE;
            if (pCreateWellKnownSid(i, NULL, &max_sid.sid, &size))
            {
                if (pConvertSidToStringSidA(&max_sid.sid, &str_sidA))
                {
                    acc_sizeA = MAX_PATH;
                    dom_sizeA = MAX_PATH;
                    if (LookupAccountSidA(NULL, &max_sid.sid, accountA, &acc_sizeA, domainA, &dom_sizeA, &use))
                        trace(" %d: %s %s\\%s %d\n", i, str_sidA, domainA, accountA, use);
                    LocalFree(str_sidA);
                }
            }
            else
            {
                if (GetLastError() != ERROR_INVALID_PARAMETER)
                    trace(" CreateWellKnownSid(%d) failed: %d\n", i, GetLastError());
                else
                    trace(" %d: not supported\n", i);
            }
        }

        pLsaQueryInformationPolicy = (fnLsaQueryInformationPolicy)GetProcAddress( hmod, "LsaQueryInformationPolicy");
        pLsaOpenPolicy = (fnLsaOpenPolicy)GetProcAddress( hmod, "LsaOpenPolicy");
        pLsaFreeMemory = (fnLsaFreeMemory)GetProcAddress( hmod, "LsaFreeMemory");
        pLsaClose = (fnLsaClose)GetProcAddress( hmod, "LsaClose");

        if (pLsaQueryInformationPolicy && pLsaOpenPolicy && pLsaFreeMemory && pLsaClose)
        {
            NTSTATUS status;
            LSA_HANDLE handle;
            LSA_OBJECT_ATTRIBUTES object_attributes;

            ZeroMemory(&object_attributes, sizeof(object_attributes));
            object_attributes.Length = sizeof(object_attributes);

            status = pLsaOpenPolicy( NULL, &object_attributes, POLICY_ALL_ACCESS, &handle);
            ok(status == STATUS_SUCCESS || status == STATUS_ACCESS_DENIED,
               "LsaOpenPolicy(POLICY_ALL_ACCESS) returned 0x%08x\n", status);

            /* try a more restricted access mask if necessary */
            if (status == STATUS_ACCESS_DENIED) {
                trace("LsaOpenPolicy(POLICY_ALL_ACCESS) failed, trying POLICY_VIEW_LOCAL_INFORMATION\n");
                status = pLsaOpenPolicy( NULL, &object_attributes, POLICY_VIEW_LOCAL_INFORMATION, &handle);
                ok(status == STATUS_SUCCESS, "LsaOpenPolicy(POLICY_VIEW_LOCAL_INFORMATION) returned 0x%08x\n", status);
            }

            if (status == STATUS_SUCCESS)
            {
                PPOLICY_ACCOUNT_DOMAIN_INFO info;
                status = pLsaQueryInformationPolicy(handle, PolicyAccountDomainInformation, (PVOID*)&info);
                ok(status == STATUS_SUCCESS, "LsaQueryInformationPolicy() failed, returned 0x%08x\n", status);
                if (status == STATUS_SUCCESS)
                {
                    ok(info->DomainSid!=0, "LsaQueryInformationPolicy(PolicyAccountDomainInformation) missing SID\n");
                    if (info->DomainSid)
                    {
                        int count = *GetSidSubAuthorityCount(info->DomainSid);
                        CopySid(GetSidLengthRequired(count), &max_sid, info->DomainSid);
                        test_sid_str((PSID)&max_sid.sid);
                        max_sid.sid.SubAuthority[count] = DOMAIN_USER_RID_ADMIN;
                        max_sid.sid.SubAuthorityCount = count + 1;
                        test_sid_str((PSID)&max_sid.sid);
                        max_sid.sid.SubAuthority[count] = DOMAIN_USER_RID_GUEST;
                        test_sid_str((PSID)&max_sid.sid);
                        max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_ADMINS;
                        test_sid_str((PSID)&max_sid.sid);
                        max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_USERS;
                        test_sid_str((PSID)&max_sid.sid);
                        max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_GUESTS;
                        test_sid_str((PSID)&max_sid.sid);
                        max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_COMPUTERS;
                        test_sid_str((PSID)&max_sid.sid);
                        max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_CONTROLLERS;
                        test_sid_str((PSID)&max_sid.sid);
                        max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_CERT_ADMINS;
                        test_sid_str((PSID)&max_sid.sid);
                        max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_SCHEMA_ADMINS;
                        test_sid_str((PSID)&max_sid.sid);
                        max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_ENTERPRISE_ADMINS;
                        test_sid_str((PSID)&max_sid.sid);
                        max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_POLICY_ADMINS;
                        test_sid_str((PSID)&max_sid.sid);
                        max_sid.sid.SubAuthority[count] = DOMAIN_ALIAS_RID_RAS_SERVERS;
                        test_sid_str((PSID)&max_sid.sid);
                        max_sid.sid.SubAuthority[count] = 1000;	/* first user account */
                        test_sid_str((PSID)&max_sid.sid);
                    }

                    pLsaFreeMemory((LPVOID)info);
                }

                status = pLsaClose(handle);
                ok(status == STATUS_SUCCESS, "LsaClose() failed, returned 0x%08x\n", status);
            }
        }
    }
}

static void get_sid_info(PSID psid, LPSTR *user, LPSTR *dom)
{
    static CHAR account[UNLEN + 1];
    static CHAR domain[UNLEN + 1];
    DWORD size, dom_size;
    SID_NAME_USE use;

    *user = account;
    *dom = domain;

    size = dom_size = UNLEN + 1;
    account[0] = '\0';
    domain[0] = '\0';
    LookupAccountSidA(NULL, psid, account, &size, domain, &dom_size, &use);
}

static void test_LookupAccountName(void)
{
    DWORD sid_size, domain_size, user_size;
    DWORD sid_save, domain_save;
    CHAR user_name[UNLEN + 1];
    SID_NAME_USE sid_use;
    LPSTR domain, account, sid_dom;
    PSID psid;
    BOOL ret;

    /* native crashes if (assuming all other parameters correct):
     *  - peUse is NULL
     *  - Sid is NULL and cbSid is > 0
     *  - cbSid or cchReferencedDomainName are NULL
     *  - ReferencedDomainName is NULL and cchReferencedDomainName is the correct size
     */

    user_size = UNLEN + 1;
    ret = GetUserNameA(user_name, &user_size);
    ok(ret, "Failed to get user name\n");

    /* get sizes */
    sid_size = 0;
    domain_size = 0;
    sid_use = 0xcafebabe;
    SetLastError(0xdeadbeef);
    ret = LookupAccountNameA(NULL, user_name, NULL, &sid_size, NULL, &domain_size, &sid_use);
    ok(!ret, "Expected 0, got %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %d\n", GetLastError());
    ok(sid_size != 0, "Expected non-zero sid size\n");
    ok(domain_size != 0, "Expected non-zero domain size\n");
    ok(sid_use == 0xcafebabe, "Expected 0xcafebabe, got %d\n", sid_use);

    sid_save = sid_size;
    domain_save = domain_size;

    psid = HeapAlloc(GetProcessHeap(), 0, sid_size);
    domain = HeapAlloc(GetProcessHeap(), 0, domain_size);

    /* try valid account name */
    ret = LookupAccountNameA(NULL, user_name, psid, &sid_size, domain, &domain_size, &sid_use);
    get_sid_info(psid, &account, &sid_dom);
    ok(ret, "Failed to lookup account name\n");
    ok(sid_size == GetLengthSid(psid), "Expected %d, got %d\n", GetLengthSid(psid), sid_size);
    todo_wine
    {
        ok(!lstrcmp(account, user_name), "Expected %s, got %s\n", user_name, account);
        ok(!lstrcmp(domain, sid_dom), "Expected %s, got %s\n", sid_dom, domain);
        ok(domain_size == domain_save - 1, "Expected %d, got %d\n", domain_save - 1, domain_size);
        ok(lstrlen(domain) == domain_size, "Expected %d\n", lstrlen(domain));
        ok(sid_use == SidTypeUser, "Expected SidTypeUser, got %d\n", SidTypeUser);
    }
    domain_size = domain_save;

    /* NULL Sid with zero sid size */
    SetLastError(0xdeadbeef);
    sid_size = 0;
    ret = LookupAccountNameA(NULL, user_name, NULL, &sid_size, domain, &domain_size, &sid_use);
    ok(!ret, "Expected 0, got %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %d\n", GetLastError());
    ok(sid_size == sid_save, "Expected %d, got %d\n", sid_save, sid_size);
    ok(domain_size == domain_save, "Expected %d, got %d\n", domain_save, domain_size);

    /* try cchReferencedDomainName - 1 */
    SetLastError(0xdeadbeef);
    domain_size--;
    ret = LookupAccountNameA(NULL, user_name, NULL, &sid_size, domain, &domain_size, &sid_use);
    ok(!ret, "Expected 0, got %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %d\n", GetLastError());
    ok(sid_size == sid_save, "Expected %d, got %d\n", sid_save, sid_size);
    ok(domain_size == domain_save, "Expected %d, got %d\n", domain_save, domain_size);

    /* NULL ReferencedDomainName with zero domain name size */
    SetLastError(0xdeadbeef);
    domain_size = 0;
    ret = LookupAccountNameA(NULL, user_name, psid, &sid_size, NULL, &domain_size, &sid_use);
    ok(!ret, "Expected 0, got %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %d\n", GetLastError());
    ok(sid_size == sid_save, "Expected %d, got %d\n", sid_save, sid_size);
    ok(domain_size == domain_save, "Expected %d, got %d\n", domain_save, domain_size);

    HeapFree(GetProcessHeap(), 0, psid);
    HeapFree(GetProcessHeap(), 0, domain);

    /* get sizes for NULL account name */
    sid_size = 0;
    domain_size = 0;
    sid_use = 0xcafebabe;
    SetLastError(0xdeadbeef);
    ret = LookupAccountNameA(NULL, NULL, NULL, &sid_size, NULL, &domain_size, &sid_use);
    ok(!ret, "Expected 0, got %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %d\n", GetLastError());
    ok(sid_size != 0, "Expected non-zero sid size\n");
    ok(domain_size != 0, "Expected non-zero domain size\n");
    ok(sid_use == 0xcafebabe, "Expected 0xcafebabe, got %d\n", sid_use);

    psid = HeapAlloc(GetProcessHeap(), 0, sid_size);
    domain = HeapAlloc(GetProcessHeap(), 0, domain_size);

    /* try NULL account name */
    ret = LookupAccountNameA(NULL, NULL, psid, &sid_size, domain, &domain_size, &sid_use);
    get_sid_info(psid, &account, &sid_dom);
    ok(ret, "Failed to lookup account name\n");
    todo_wine
    {
        ok(!lstrcmp(account, "BUILTIN"), "Expected BUILTIN, got %s\n", account);
        ok(!lstrcmp(domain, "BUILTIN"), "Expected BUILTIN, got %s\n", domain);
        ok(sid_use == SidTypeDomain, "Expected SidTypeDomain, got %d\n", SidTypeDomain);
    }

    /* try an invalid account name */
    SetLastError(0xdeadbeef);
    sid_size = 0;
    domain_size = 0;
    ret = LookupAccountNameA(NULL, "oogabooga", NULL, &sid_size, NULL, &domain_size, &sid_use);
    ok(!ret, "Expected 0, got %d\n", ret);
    todo_wine
    {
        ok(GetLastError() == ERROR_NONE_MAPPED,
           "Expected ERROR_NONE_MAPPED, got %d\n", GetLastError());
        ok(sid_size == 0, "Expected 0, got %d\n", sid_size);
        ok(domain_size == 0, "Expected 0, got %d\n", domain_size);
    }

    HeapFree(GetProcessHeap(), 0, psid);
    HeapFree(GetProcessHeap(), 0, domain);
}

#define TEST_GRANTED_ACCESS(a,b) test_granted_access(a,b,__LINE__)
static void test_granted_access(HANDLE handle, ACCESS_MASK access, int line)
{
    OBJECT_BASIC_INFORMATION obj_info;
    NTSTATUS status;

    if (!pNtQueryObject)
    {
        skip_(__FILE__, line)("Not NT platform - skipping tests\n");
        return;
    }

    status = pNtQueryObject( handle, ObjectBasicInformation, &obj_info,
                             sizeof(obj_info), NULL );
    ok_(__FILE__, line)(!status, "NtQueryObject with err: %08x\n", status);
    ok_(__FILE__, line)(obj_info.GrantedAccess == access, "Gratned access should "
        "be 0x%08x, instead of 0x%08x\n", access, obj_info.GrantedAccess);
}

#define CHECK_SET_SECURITY(o,i,e) \
    do{ \
        BOOL res; \
        DWORD err; \
        SetLastError( 0xdeadbeef ); \
        res = SetKernelObjectSecurity( o, i, SecurityDescriptor ); \
        err = GetLastError(); \
        if (e == ERROR_SUCCESS) \
            ok(res, "SetKernelObjectSecurity failed with %d\n", err); \
        else \
            ok(!res && err == e, "SetKernelObjectSecurity should have failed " \
               "with %s, instead of %d\n", #e, err); \
    }while(0)

static void test_process_security(void)
{
    BOOL res;
    char owner[32], group[32];
    PSID AdminSid = NULL, UsersSid = NULL;
    PACL Acl = NULL;
    SECURITY_DESCRIPTOR *SecurityDescriptor = NULL;
    char buffer[MAX_PATH];
    PROCESS_INFORMATION info;
    STARTUPINFOA startup;
    SECURITY_ATTRIBUTES psa;
    HANDLE token, event;
    DWORD tmp;

    Acl = HeapAlloc(GetProcessHeap(), 0, 256);
    res = InitializeAcl(Acl, 256, ACL_REVISION);
    if (!res && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        skip("ACLs not implemented - skipping tests\n");
        return;
    }
    ok(res, "InitializeAcl failed with error %d\n", GetLastError());

    /* get owner from the token we might be running as a user not admin */
    res = OpenProcessToken( GetCurrentProcess(), MAXIMUM_ALLOWED, &token );
    ok(res, "OpenProcessToken failed with error %d\n", GetLastError());
    if (!res) return;

    res = GetTokenInformation( token, TokenOwner, owner, sizeof(owner), &tmp );
    ok(res, "GetTokenInformation failed with error %d\n", GetLastError());
    AdminSid = ((TOKEN_OWNER*)owner)->Owner;
    res = GetTokenInformation( token, TokenPrimaryGroup, group, sizeof(group), &tmp );
    ok(res, "GetTokenInformation failed with error %d\n", GetLastError());
    UsersSid = ((TOKEN_PRIMARY_GROUP*)group)->PrimaryGroup;

    CloseHandle( token );
    if (!res) return;

    res = AddAccessDeniedAce(Acl, ACL_REVISION, PROCESS_VM_READ, AdminSid);
    ok(res, "AddAccessDeniedAce failed with error %d\n", GetLastError());
    res = AddAccessAllowedAce(Acl, ACL_REVISION, PROCESS_ALL_ACCESS, AdminSid);
    ok(res, "AddAccessAllowedAceEx failed with error %d\n", GetLastError());

    SecurityDescriptor = HeapAlloc(GetProcessHeap(), 0, SECURITY_DESCRIPTOR_MIN_LENGTH);
    res = InitializeSecurityDescriptor(SecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    ok(res, "InitializeSecurityDescriptor failed with error %d\n", GetLastError());

    event = CreateEvent( NULL, TRUE, TRUE, "test_event" );
    ok(event != NULL, "CreateEvent %d\n", GetLastError());

    CHECK_SET_SECURITY( event, OWNER_SECURITY_INFORMATION, ERROR_INVALID_SECURITY_DESCR );
    CHECK_SET_SECURITY( event, GROUP_SECURITY_INFORMATION, ERROR_INVALID_SECURITY_DESCR );
    CHECK_SET_SECURITY( event, SACL_SECURITY_INFORMATION, ERROR_ACCESS_DENIED );
    CHECK_SET_SECURITY( event, DACL_SECURITY_INFORMATION, ERROR_SUCCESS );

    /* Set owner and group and dacl */
    res = SetSecurityDescriptorOwner(SecurityDescriptor, AdminSid, FALSE);
    ok(res, "SetSecurityDescriptorOwner failed with error %d\n", GetLastError());
    CHECK_SET_SECURITY( event, OWNER_SECURITY_INFORMATION, ERROR_SUCCESS );
    res = SetSecurityDescriptorGroup(SecurityDescriptor, UsersSid, FALSE);
    ok(res, "SetSecurityDescriptorGroup failed with error %d\n", GetLastError());
    CHECK_SET_SECURITY( event, GROUP_SECURITY_INFORMATION, ERROR_SUCCESS );
    res = SetSecurityDescriptorDacl(SecurityDescriptor, TRUE, Acl, FALSE);
    ok(res, "SetSecurityDescriptorDacl failed with error %d\n", GetLastError());
    CHECK_SET_SECURITY( event, DACL_SECURITY_INFORMATION, ERROR_SUCCESS );

    sprintf(buffer, "%s tests/security.c test", myARGV[0]);
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;

    psa.nLength = sizeof(psa);
    psa.lpSecurityDescriptor = SecurityDescriptor;
    psa.bInheritHandle = TRUE;

    /* Doesn't matter what ACL say we should get full access for ourselves */
    ok(CreateProcessA( NULL, buffer, &psa, NULL, FALSE, 0, NULL, NULL, &startup, &info ),
        "CreateProcess with err:%d\n", GetLastError());
    TEST_GRANTED_ACCESS( info.hProcess, PROCESS_ALL_ACCESS );
    ok(WaitForSingleObject(info.hProcess, 30000) == WAIT_OBJECT_0, "Child process termination\n");

    CloseHandle( event );
    HeapFree(GetProcessHeap(), 0, Acl);
    HeapFree(GetProcessHeap(), 0, SecurityDescriptor);
}

static void test_process_security_child(void)
{
    HANDLE handle, handle1;
    BOOL ret;
    DWORD err;

    handle = OpenProcess( PROCESS_TERMINATE, FALSE, GetCurrentProcessId() );
    ok(handle != NULL, "OpenProcess(PROCESS_TERMINATE) with err:%d\n", GetLastError());
    TEST_GRANTED_ACCESS( handle, PROCESS_TERMINATE );

    ok(DuplicateHandle( GetCurrentProcess(), handle, GetCurrentProcess(),
                        &handle1, 0, TRUE, DUPLICATE_SAME_ACCESS ),
       "duplicating handle err:%d\n", GetLastError());
    TEST_GRANTED_ACCESS( handle1, PROCESS_TERMINATE );

    CloseHandle( handle1 );

    SetLastError( 0xdeadbeef );
    ret = DuplicateHandle( GetCurrentProcess(), handle, GetCurrentProcess(),
                           &handle1, PROCESS_ALL_ACCESS, TRUE, 0 );
    err = GetLastError();
    todo_wine
    ok(!ret && err == ERROR_ACCESS_DENIED, "duplicating handle should have failed "
       "with STATUS_ACCESS_DENIED, instead of err:%d\n", err);

    CloseHandle( handle );

    /* These two should fail - they are denied by ACL */
    handle = OpenProcess( PROCESS_VM_READ, FALSE, GetCurrentProcessId() );
    todo_wine
    ok(handle == NULL, "OpenProcess(PROCESS_VM_READ) should have failed\n");
    handle = OpenProcess( PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId() );
    todo_wine
    ok(handle == NULL, "OpenProcess(PROCESS_ALL_ACCESS) should have failed\n");

    /* Documented privilege elevation */
    ok(DuplicateHandle( GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(),
                        &handle, 0, TRUE, DUPLICATE_SAME_ACCESS ),
       "duplicating handle err:%d\n", GetLastError());
    TEST_GRANTED_ACCESS( handle, PROCESS_ALL_ACCESS );

    CloseHandle( handle );

    /* Same only explicitly asking for all access rights */
    ok(DuplicateHandle( GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(),
                        &handle, PROCESS_ALL_ACCESS, TRUE, 0 ),
       "duplicating handle err:%d\n", GetLastError());
    TEST_GRANTED_ACCESS( handle, PROCESS_ALL_ACCESS );
    ok(DuplicateHandle( GetCurrentProcess(), handle, GetCurrentProcess(),
                        &handle1, PROCESS_VM_READ, TRUE, 0 ),
       "duplicating handle err:%d\n", GetLastError());
    TEST_GRANTED_ACCESS( handle1, PROCESS_VM_READ );
    CloseHandle( handle1 );
    CloseHandle( handle );
}

static void test_impersonation_level(void)
{
    HANDLE Token, ProcessToken;
    HANDLE Token2;
    DWORD Size;
    TOKEN_PRIVILEGES *Privileges;
    TOKEN_USER *User;
    PRIVILEGE_SET *PrivilegeSet;
    BOOL AccessGranted;
    BOOL ret;
    HKEY hkey;
    DWORD error;

    ret = ImpersonateSelf(SecurityAnonymous);
    ok(ret, "ImpersonateSelf(SecurityAnonymous) failed with error %d\n", GetLastError());
    ret = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY_SOURCE | TOKEN_IMPERSONATE | TOKEN_ADJUST_DEFAULT, TRUE, &Token);
    ok(!ret, "OpenThreadToken should have failed\n");
    error = GetLastError();
    ok(error == ERROR_CANT_OPEN_ANONYMOUS, "OpenThreadToken on anonymous token should have returned ERROR_CANT_OPEN_ANONYMOUS instead of %d\n", error);
    /* can't perform access check when opening object against an anonymous impersonation token */
    todo_wine {
    error = RegOpenKeyEx(HKEY_CURRENT_USER, "Software", 0, KEY_READ, &hkey);
    ok(error == ERROR_INVALID_HANDLE, "RegOpenKeyEx should have failed with ERROR_INVALID_HANDLE instead of %d\n", error);
    }
    RevertToSelf();

    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &ProcessToken);
    ok(ret, "OpenProcessToken failed with error %d\n", GetLastError());

    ret = DuplicateTokenEx(ProcessToken,
        TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE, NULL,
        SecurityAnonymous, TokenImpersonation, &Token);
    ok(ret, "DuplicateTokenEx failed with error %d\n", GetLastError());
    /* can't increase the impersonation level */
    ret = DuplicateToken(Token, SecurityIdentification, &Token2);
    error = GetLastError();
    ok(!ret && error == ERROR_BAD_IMPERSONATION_LEVEL,
        "Duplicating a token and increasing the impersonation level should have failed with ERROR_BAD_IMPERSONATION_LEVEL instead of %d\n", error);
    /* we can query anything from an anonymous token, including the user */
    ret = GetTokenInformation(Token, TokenUser, NULL, 0, &Size);
    error = GetLastError();
    ok(!ret && error == ERROR_INSUFFICIENT_BUFFER, "GetTokenInformation(TokenUser) should have failed with ERROR_INSUFFICIENT_BUFFER instead of %d\n", error);
    User = (TOKEN_USER *)HeapAlloc(GetProcessHeap(), 0, Size);
    ret = GetTokenInformation(Token, TokenUser, User, Size, &Size);
    ok(ret, "GetTokenInformation(TokenUser) failed with error %d\n", GetLastError());
    HeapFree(GetProcessHeap(), 0, User);

    /* PrivilegeCheck fails with SecurityAnonymous level */
    ret = GetTokenInformation(Token, TokenPrivileges, NULL, 0, &Size);
    error = GetLastError();
    ok(!ret && error == ERROR_INSUFFICIENT_BUFFER, "GetTokenInformation(TokenPrivileges) should have failed with ERROR_INSUFFICIENT_BUFFER instead of %d\n", error);
    Privileges = (TOKEN_PRIVILEGES *)HeapAlloc(GetProcessHeap(), 0, Size);
    ret = GetTokenInformation(Token, TokenPrivileges, Privileges, Size, &Size);
    ok(ret, "GetTokenInformation(TokenPrivileges) failed with error %d\n", GetLastError());

    PrivilegeSet = (PRIVILEGE_SET *)HeapAlloc(GetProcessHeap(), 0, FIELD_OFFSET(PRIVILEGE_SET, Privilege[Privileges->PrivilegeCount]));
    PrivilegeSet->PrivilegeCount = Privileges->PrivilegeCount;
    memcpy(PrivilegeSet->Privilege, Privileges->Privileges, PrivilegeSet->PrivilegeCount * sizeof(PrivilegeSet->Privilege[0]));
    PrivilegeSet->Control = PRIVILEGE_SET_ALL_NECESSARY;
    HeapFree(GetProcessHeap(), 0, Privileges);

    ret = PrivilegeCheck(Token, PrivilegeSet, &AccessGranted);
    error = GetLastError();
    ok(!ret && error == ERROR_BAD_IMPERSONATION_LEVEL, "PrivilegeCheck for SecurityAnonymous token should have failed with ERROR_BAD_IMPERSONATION_LEVEL instead of %d\n", error);

    CloseHandle(Token);

    ret = ImpersonateSelf(SecurityIdentification);
    ok(ret, "ImpersonateSelf(SecurityIdentification) failed with error %d\n", GetLastError());
    ret = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY_SOURCE | TOKEN_IMPERSONATE | TOKEN_ADJUST_DEFAULT, TRUE, &Token);
    ok(ret, "OpenThreadToken failed with error %d\n", GetLastError());

    /* can't perform access check when opening object against an identification impersonation token */
    error = RegOpenKeyEx(HKEY_CURRENT_USER, "Software", 0, KEY_READ, &hkey);
    todo_wine {
    ok(error == ERROR_INVALID_HANDLE, "RegOpenKeyEx should have failed with ERROR_INVALID_HANDLE instead of %d\n", error);
    }
    ret = PrivilegeCheck(Token, PrivilegeSet, &AccessGranted);
    ok(ret, "PrivilegeCheck for SecurityIdentification failed with error %d\n", GetLastError());
    CloseHandle(Token);
    RevertToSelf();

    ret = ImpersonateSelf(SecurityImpersonation);
    ok(ret, "ImpersonateSelf(SecurityImpersonation) failed with error %d\n", GetLastError());
    ret = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY_SOURCE | TOKEN_IMPERSONATE | TOKEN_ADJUST_DEFAULT, TRUE, &Token);
    ok(ret, "OpenThreadToken failed with error %d\n", GetLastError());
    error = RegOpenKeyEx(HKEY_CURRENT_USER, "Software", 0, KEY_READ, &hkey);
    ok(error == ERROR_SUCCESS, "RegOpenKeyEx should have succeeded instead of failing with %d\n", error);
    RegCloseKey(hkey);
    ret = PrivilegeCheck(Token, PrivilegeSet, &AccessGranted);
    ok(ret, "PrivilegeCheck for SecurityImpersonation failed with error %d\n", GetLastError());
    RevertToSelf();

    CloseHandle(Token);
    CloseHandle(ProcessToken);

    HeapFree(GetProcessHeap(), 0, PrivilegeSet);
}

START_TEST(security)
{
    init();
    if (!hmod) return;

    if (myARGC >= 3)
    {
        test_process_security_child();
        return;
    }
    test_sid();
    test_trustee();
    test_luid();
    test_FileSecurity();
    test_AccessCheck();
    test_token_attr();
    test_LookupAccountSid();
    test_LookupAccountName();
    test_process_security();
    test_impersonation_level();
}
