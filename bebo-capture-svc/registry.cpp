// Copyright 2015 The Chromium Authors. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//    * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//    * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "registry.h"

#include <shlwapi.h>
#include <stddef.h>
#include <algorithm>
#include <string.h>

// The arraysize(arr) macro returns the # of elements in an array arr.  The
// expression is a compile-time constant, and therefore can be used in defining
// new arrays, for example.  If you use arraysize on a pointer by mistake, you
// will get a compile-time error.  For the technical details, refer to
// http://blogs.msdn.com/b/the1/archive/2004/05/07/128242.aspx.

// This template function declaration is used in defining arraysize.
// Note that the function doesn't need an implementation, as we only
// use its type.
template <typename T, size_t N> char(&ArraySizeHelper(T(&array)[N]))[N];
#define arraysize(array) (sizeof(ArraySizeHelper(array)))
#include <assert.h>

// RegEnumValue() reports the number of characters from the name that were
// written to the buffer, not how many there are. This constant is the maximum
// name size, such that a buffer with this size should read any name.
const DWORD MAX_REGISTRY_NAME_SIZE = 16384;

// Registry values are read as BYTE* but can have wchar_t* data whose last
// wchar_t is truncated. This function converts the reported |byte_size| to
// a size in wchar_t that can store a truncated wchar_t if necessary.
inline DWORD to_wchar_size(DWORD byte_size) {
	return (byte_size + sizeof(wchar_t) - 1) / sizeof(wchar_t);
}

// Mask to pull WOW64 access flags out of REGSAM access.
const REGSAM kWow64AccessMask = KEY_WOW64_32KEY | KEY_WOW64_64KEY;

// RegKey ----------------------------------------------------------------------

RegKey::RegKey() : key_(NULL), wow64access_(0) {
}

RegKey::RegKey(HKEY key) : key_(key), wow64access_(0) {
}

RegKey::RegKey(HKEY rootkey, const wchar_t* subkey, REGSAM access)
	: key_(NULL),
	wow64access_(0) {
	if (rootkey) {
		if (access & (KEY_SET_VALUE | KEY_CREATE_SUB_KEY | KEY_CREATE_LINK))
			Create(rootkey, subkey, access);
		else
			Open(rootkey, subkey, access);
	}
	else {
		wow64access_ = access & kWow64AccessMask;
	}
}

RegKey::~RegKey() {
	Close();
}

LONG RegKey::Create(HKEY rootkey, const wchar_t* subkey, REGSAM access) {
	DWORD disposition_value;
	return CreateWithDisposition(rootkey, subkey, &disposition_value, access);
}

LONG RegKey::CreateWithDisposition(HKEY rootkey, const wchar_t* subkey,
	DWORD* disposition, REGSAM access) {
	HKEY subhkey = NULL;
	LONG result = RegCreateKeyEx(rootkey, subkey, 0, NULL,
		REG_OPTION_NON_VOLATILE, access, NULL, &subhkey,
		disposition);
	if (result == ERROR_SUCCESS) {
		Close();
		key_ = subhkey;
		wow64access_ = access & kWow64AccessMask;
	}

	return result;
}

LONG RegKey::CreateKey(const wchar_t* name, REGSAM access) {
	// After the application has accessed an alternate registry view using one of
	// the [KEY_WOW64_32KEY / KEY_WOW64_64KEY] flags, all subsequent operations
	// (create, delete, or open) on child registry keys must explicitly use the
	// same flag. Otherwise, there can be unexpected behavior.
	// http://msdn.microsoft.com/en-us/library/windows/desktop/aa384129.aspx.
	if ((access & kWow64AccessMask) != wow64access_) {
		return ERROR_INVALID_PARAMETER;
	}
	HKEY subkey = NULL;
	LONG result = RegCreateKeyEx(key_, name, 0, NULL, REG_OPTION_NON_VOLATILE,
		access, NULL, &subkey, NULL);
	if (result == ERROR_SUCCESS) {
		Close();
		key_ = subkey;
		wow64access_ = access & kWow64AccessMask;
	}

	return result;
}

LONG RegKey::Open(HKEY rootkey, const wchar_t* subkey, REGSAM access) {
	HKEY subhkey = NULL;

	LONG result = RegOpenKeyEx(rootkey, subkey, 0, access, &subhkey);
	if (result == ERROR_SUCCESS) {
		Close();
		key_ = subhkey;
		wow64access_ = access & kWow64AccessMask;
	}

	return result;
}

LONG RegKey::OpenKey(const wchar_t* relative_key_name, REGSAM access) {
	// After the application has accessed an alternate registry view using one of
	// the [KEY_WOW64_32KEY / KEY_WOW64_64KEY] flags, all subsequent operations
	// (create, delete, or open) on child registry keys must explicitly use the
	// same flag. Otherwise, there can be unexpected behavior.
	// http://msdn.microsoft.com/en-us/library/windows/desktop/aa384129.aspx.
	if ((access & kWow64AccessMask) != wow64access_) {
		return ERROR_INVALID_PARAMETER;
	}
	HKEY subkey = NULL;
	LONG result = RegOpenKeyEx(key_, relative_key_name, 0, access, &subkey);

	// We have to close the current opened key before replacing it with the new
	// one.
	if (result == ERROR_SUCCESS) {
		Close();
		key_ = subkey;
		wow64access_ = access & kWow64AccessMask;
	}
	return result;
}

void RegKey::Close() {
	if (key_) {
		::RegCloseKey(key_);
		key_ = NULL;
		wow64access_ = 0;
	}
}

// TODO(wfh): Remove this and other unsafe methods. See http://crbug.com/375400
void RegKey::Set(HKEY key) {
	if (key_ != key) {
		Close();
		key_ = key;
	}
}

HKEY RegKey::Take() {
	HKEY key = key_;
	key_ = NULL;
	return key;
}

bool RegKey::HasValue(const wchar_t* name) const {
	return RegQueryValueEx(key_, name, 0, NULL, NULL, NULL) == ERROR_SUCCESS;
}

DWORD RegKey::GetValueCount() const {
	DWORD count = 0;
	LONG result = RegQueryInfoKey(key_, NULL, 0, NULL, NULL, NULL, NULL, &count,
		NULL, NULL, NULL, NULL);
	return (result == ERROR_SUCCESS) ? count : 0;
}

LONG RegKey::GetValueNameAt(int index, std::wstring* name) const {
	wchar_t buf[256];
	DWORD bufsize = arraysize(buf);
	LONG r = ::RegEnumValue(key_, index, buf, &bufsize, NULL, NULL, NULL, NULL);
	if (r == ERROR_SUCCESS)
		*name = buf;

	return r;
}

LONG RegKey::DeleteEmptyKey(const wchar_t* name) {

	HKEY target_key = NULL;
	LONG result = RegOpenKeyEx(key_, name, 0, KEY_READ | wow64access_,
		&target_key);

	if (result != ERROR_SUCCESS)
		return result;

	DWORD count = 0;
	result = RegQueryInfoKey(target_key, NULL, 0, NULL, NULL, NULL, NULL, &count,
		NULL, NULL, NULL, NULL);

	RegCloseKey(target_key);

	if (result != ERROR_SUCCESS)
		return result;

	if (count == 0)
		return RegDeleteKeyExWrapper(key_, name, wow64access_, 0);

	return ERROR_DIR_NOT_EMPTY;
}

LONG RegKey::DeleteValue(const wchar_t* value_name) {
	LONG result = RegDeleteValue(key_, value_name);
	return result;
}

LONG RegKey::ReadValueDW(const wchar_t* name, DWORD* out_value) const {
	DWORD type = REG_DWORD;
	DWORD size = sizeof(DWORD);
	DWORD local_value = 0;
	LONG result = ReadValue(name, &local_value, &size, &type);
	if (result == ERROR_SUCCESS) {
		if ((type == REG_DWORD || type == REG_BINARY) && size == sizeof(DWORD))
			*out_value = local_value;
		else
			result = ERROR_CANTREAD;
	}

	return result;
}

LONG RegKey::ReadInt64(const wchar_t* name, int64_t* out_value) const {
	DWORD type = REG_QWORD;
	int64_t local_value = 0;
	DWORD size = sizeof(local_value);
	LONG result = ReadValue(name, &local_value, &size, &type);
	if (result == ERROR_SUCCESS) {
		if ((type == REG_QWORD || type == REG_BINARY) &&
			size == sizeof(local_value))
			*out_value = local_value;
		else
			result = ERROR_CANTREAD;
	}

	return result;
}

LONG RegKey::ReadValue(const wchar_t* name, std::wstring* out_value) const {
	const size_t kMaxStringLength = 1024;  // This is after expansion.
										   // Use the one of the other forms of ReadValue if 1024 is too small for you.
	wchar_t raw_value[kMaxStringLength];
	DWORD type = REG_SZ, size = sizeof(raw_value);
	LONG result = ReadValue(name, raw_value, &size, &type);
	if (result == ERROR_SUCCESS) {
		if (type == REG_SZ) {
			*out_value = raw_value;
		}
		else if (type == REG_EXPAND_SZ) {
			wchar_t expanded[kMaxStringLength];
			size = ExpandEnvironmentStrings(raw_value, expanded, kMaxStringLength);
			// Success: returns the number of wchar_t's copied
			// Fail: buffer too small, returns the size required
			// Fail: other, returns 0
			if (size == 0 || size > kMaxStringLength) {
				result = ERROR_MORE_DATA;
			}
			else {
				*out_value = expanded;
			}
		}
		else {
			// Not a string. Oops.
			result = ERROR_CANTREAD;
		}
	}

	return result;
}

LONG RegKey::ReadValue(const wchar_t* name,
	void* data,
	DWORD* dsize,
	DWORD* dtype) const {
	LONG result = RegQueryValueEx(key_, name, 0, dtype,
		reinterpret_cast<LPBYTE>(data), dsize);
	return result;
}

LONG RegKey::ReadValues(const wchar_t* name,
	std::vector<std::wstring>* values) {
	values->clear();

	DWORD type = REG_MULTI_SZ;
	DWORD size = 0;
	LONG result = ReadValue(name, NULL, &size, &type);
	if (result != ERROR_SUCCESS || size == 0)
		return result;

	if (type != REG_MULTI_SZ)
		return ERROR_CANTREAD;

	std::vector<wchar_t> buffer(size / sizeof(wchar_t));
	result = ReadValue(name, &buffer[0], &size, NULL);
	if (result != ERROR_SUCCESS || size == 0)
		return result;

	// Parse the double-null-terminated list of strings.
	// Note: This code is paranoid to not read outside of |buf|, in the case where
	// it may not be properly terminated.
	const wchar_t* entry = &buffer[0];
	const wchar_t* buffer_end = entry + (size / sizeof(wchar_t));
	while (entry < buffer_end && entry[0] != '\0') {
		const wchar_t* entry_end = std::find(entry, buffer_end, L'\0');
		values->push_back(std::wstring(entry, entry_end));
		entry = entry_end + 1;
	}
	return 0;
}

LONG RegKey::WriteValue(const wchar_t* name, DWORD in_value) {
	return WriteValue(
		name, &in_value, static_cast<DWORD>(sizeof(in_value)), REG_DWORD);
}

LONG RegKey::WriteValue(const wchar_t* name, int64_t in_value) {
	return WriteValue(
		name, &in_value, static_cast<int64_t>(sizeof(in_value)), REG_QWORD);
}


LONG RegKey::WriteValue(const wchar_t * name, const wchar_t* in_value) {
	return WriteValue(name, in_value,
		static_cast<DWORD>(sizeof(*in_value) * (wcslen(in_value) + 1)), REG_SZ);
}

LONG RegKey::WriteValue(const wchar_t* name,
	const void* data,
	DWORD dsize,
	DWORD dtype) {
	LONG result = RegSetValueEx(key_, name, 0, dtype,
		reinterpret_cast<LPBYTE>(const_cast<void*>(data)), dsize);
	return result;
}

// static
LONG RegKey::RegDeleteKeyExWrapper(HKEY hKey,
	const wchar_t* lpSubKey,
	REGSAM samDesired,
	DWORD Reserved) {
	typedef LSTATUS(WINAPI* RegDeleteKeyExPtr)(HKEY, LPCWSTR, REGSAM, DWORD);

	RegDeleteKeyExPtr reg_delete_key_ex_func =
		reinterpret_cast<RegDeleteKeyExPtr>(
			GetProcAddress(GetModuleHandleA("advapi32.dll"), "RegDeleteKeyExW"));

	if (reg_delete_key_ex_func)
		return reg_delete_key_ex_func(hKey, lpSubKey, samDesired, Reserved);

	// Windows XP does not support RegDeleteKeyEx, so fallback to RegDeleteKey.
	return RegDeleteKey(hKey, lpSubKey);
}

// RegistryKeyIterator --------------------------------------------------------

RegistryKeyIterator::RegistryKeyIterator(HKEY root_key,
	const wchar_t* folder_key) {
	Initialize(root_key, folder_key, 0);
}

RegistryKeyIterator::RegistryKeyIterator(HKEY root_key,
	const wchar_t* folder_key,
	REGSAM wow64access) {
	Initialize(root_key, folder_key, wow64access);
}

RegistryKeyIterator::~RegistryKeyIterator() {
	if (key_)
		::RegCloseKey(key_);
}

DWORD RegistryKeyIterator::SubkeyCount() const {
	DWORD count = 0;
	LONG result = ::RegQueryInfoKey(key_, NULL, 0, NULL, &count, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL);
	if (result != ERROR_SUCCESS)
		return 0;

	return count;
}

bool RegistryKeyIterator::Valid() const {
	return key_ != NULL && index_ >= 0;
}

void RegistryKeyIterator::operator++() {
	--index_;
	Read();
}

bool RegistryKeyIterator::Read() {
	if (Valid()) {
		DWORD ncount = arraysize(name_);
		FILETIME written;
		LONG r = ::RegEnumKeyEx(key_, index_, name_, &ncount, NULL, NULL,
			NULL, &written);
		if (ERROR_SUCCESS == r)
			return true;
	}

	name_[0] = '\0';
	return false;
}

void RegistryKeyIterator::Initialize(HKEY root_key,
	const wchar_t* folder_key,
	REGSAM wow64access) {
	LONG result =
		RegOpenKeyEx(root_key, folder_key, 0, KEY_READ | wow64access, &key_);
	if (result != ERROR_SUCCESS) {
		key_ = NULL;
	}
	else {
		DWORD count = 0;
		result = ::RegQueryInfoKey(key_, NULL, 0, NULL, &count, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL);

		if (result != ERROR_SUCCESS) {
			::RegCloseKey(key_);
			key_ = NULL;
		}
		else {
			index_ = count - 1;
		}
	}

	Read();
}