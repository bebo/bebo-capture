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

#ifndef BEBO_BASE_WIN_REGISTRY_H_
#define BEBO_BASE_WIN_REGISTRY_H_

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <memory>

#include <stddef.h>  // For size_t.

// Put this in the declarations for a class to be uncopyable.
#define DISALLOW_COPY(TypeName) \
  TypeName(const TypeName&) = delete

// Put this in the declarations for a class to be unassignable.
#define DISALLOW_ASSIGN(TypeName) TypeName& operator=(const TypeName&) = delete

// Put this in the declarations for a class to be uncopyable and unassignable.
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  DISALLOW_COPY(TypeName);                 \
  DISALLOW_ASSIGN(TypeName)
//#define BASE_EXPORT __declspec(dllimport)

// Utility class to read, write and manipulate the Windows Registry.
// Registry vocabulary primer: a "key" is like a folder, in which there
// are "values", which are <name, data> pairs, with an associated data type.
//
// Note:
//  * ReadValue family of functions guarantee that the out-parameter
//    is not touched in case of failure.
//  * Functions returning LONG indicate success as ERROR_SUCCESS or an
//    error as a (non-zero) win32 error code.
class RegKey {
public:
	RegKey();
	explicit RegKey(HKEY key);
	RegKey(HKEY rootkey, const wchar_t* subkey, REGSAM access);
	~RegKey();

	LONG Create(HKEY rootkey, const wchar_t* subkey, REGSAM access);

	LONG CreateWithDisposition(HKEY rootkey, const wchar_t* subkey,
		DWORD* disposition, REGSAM access);

	// Creates a subkey or open it if it already exists.
	LONG CreateKey(const wchar_t* name, REGSAM access);

	// Opens an existing reg key.
	LONG Open(HKEY rootkey, const wchar_t* subkey, REGSAM access);

	// Opens an existing reg key, given the relative key name.
	LONG OpenKey(const wchar_t* relative_key_name, REGSAM access);

	// Closes this reg key.
	void Close();

	// Replaces the handle of the registry key and takes ownership of the handle.
	void Set(HKEY key);

	// Transfers ownership away from this object.
	HKEY Take();

	// Returns false if this key does not have the specified value, or if an error
	// occurrs while attempting to access it.
	bool HasValue(const wchar_t* value_name) const;

	// Returns the number of values for this key, or 0 if the number cannot be
	// determined.
	DWORD GetValueCount() const;

	// Determines the nth value's name.
	LONG GetValueNameAt(int index, std::wstring* name) const;

	// True while the key is valid.
	bool Valid() const { return key_ != NULL; }

	// Deletes an empty subkey.  If the subkey has subkeys or values then this
	// will fail.
	LONG DeleteEmptyKey(const wchar_t* name);

	// Deletes a single value within the key.
	LONG DeleteValue(const wchar_t* name);

	// Getters:

	// Reads a REG_DWORD (uint32_t) into |out_value|. If |name| is null or empty,
	// reads the key's default value, if any.
	LONG ReadValueDW(const wchar_t* name, DWORD* out_value) const;

	// Reads a REG_QWORD (int64_t) into |out_value|. If |name| is null or empty,
	// reads the key's default value, if any.
	LONG ReadInt64(const wchar_t* name, int64_t* out_value) const;

	// Reads a string into |out_value|. If |name| is null or empty, reads
	// the key's default value, if any.
	LONG ReadValue(const wchar_t* name, std::wstring* out_value) const;

	// Reads a REG_MULTI_SZ registry field into a vector of strings. Clears
	// |values| initially and adds further strings to the list. Returns
	// ERROR_CANTREAD if type is not REG_MULTI_SZ.
	LONG ReadValues(const wchar_t* name, std::vector<std::wstring>* values);

	// Reads raw data into |data|. If |name| is null or empty, reads the key's
	// default value, if any.
	LONG ReadValue(const wchar_t* name,
		void* data,
		DWORD* dsize,
		DWORD* dtype) const;

	// Setters:

	// Sets an int32_t value.
	LONG WriteValue(const wchar_t* name, DWORD in_value);

	// Sets an int64_t value.
	LONG RegKey::WriteValue(const wchar_t* name, int64_t in_value);

	// Sets a string value.
	LONG WriteValue(const wchar_t* name, const wchar_t* in_value);

	// Sets raw data, including type.
	LONG WriteValue(const wchar_t* name,
		const void* data,
		DWORD dsize,
		DWORD dtype);

	HKEY Handle() const { return key_; }

private:
	// Calls RegDeleteKeyEx on supported platforms, alternatively falls back to
	// RegDeleteKey.
	static LONG RegDeleteKeyExWrapper(HKEY hKey,
		const wchar_t* lpSubKey,
		REGSAM samDesired,
		DWORD Reserved);

	HKEY key_;  // The registry key being iterated.
	REGSAM wow64access_;

	DISALLOW_COPY_AND_ASSIGN(RegKey);
};

class RegistryKeyIterator {
public:
	// Constructs a Registry Key Iterator with default WOW64 access.
	RegistryKeyIterator(HKEY root_key, const wchar_t* folder_key);

	// Constructs a Registry Value Iterator with specific WOW64 access, one of
	// KEY_WOW64_32KEY or KEY_WOW64_64KEY, or 0.
	// Note: |wow64access| should be the same access used to open |root_key|
	// previously, or a predefined key (e.g. HKEY_LOCAL_MACHINE).
	// See http://msdn.microsoft.com/en-us/library/windows/desktop/aa384129.aspx.
	RegistryKeyIterator(HKEY root_key,
		const wchar_t* folder_key,
		REGSAM wow64access);

	~RegistryKeyIterator();

	DWORD SubkeyCount() const;

	// True while the iterator is valid.
	bool Valid() const;

	// Advances to the next entry in the folder.
	void operator++();

	const wchar_t* Name() const { return name_; }

	int Index() const { return index_; }

private:
	// Reads in the current values.
	bool Read();

	void Initialize(HKEY root_key, const wchar_t* folder_key, REGSAM wow64access);

	// The registry key being iterated.
	HKEY key_;

	// Current index of the iteration.
	int index_;

	wchar_t name_[MAX_PATH];

	DISALLOW_COPY_AND_ASSIGN(RegistryKeyIterator);
};

#endif  // BEBO_BASE_WIN_REGISTRY_H_
