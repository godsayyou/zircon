// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <trace/handler.h>

#include <zx/eventpair.h>
#include <zx/vmo.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/macros.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <hash/hash.h>

namespace trace {
namespace internal {

class TraceHandlerImpl final : public trace::TraceHandler {
public:
    static zx_status_t StartEngine(async_t* async, zx::vmo buffer,
                                   zx::eventpair fence,
                                   fbl::Vector<fbl::String> enabled_categories);
    static zx_status_t StopEngine();

private:
    TraceHandlerImpl(void* buffer, size_t buffer_num_bytes,
                     zx::eventpair fence,
                     fbl::Vector<fbl::String> enabled_categories);
    ~TraceHandlerImpl() override;

    // |trace::TraceHandler|
    bool IsCategoryEnabled(const char* category) override;
    void TraceStopped(async_t* async,
                      zx_status_t disposition, size_t buffer_bytes_written) override;

    void* buffer_;
    size_t buffer_num_bytes_;
    zx::eventpair fence_;
    fbl::Vector<fbl::String> const enabled_categories_;

    using CString = const char*;

    // For faster implementation of IsCategoryEnabled().
    struct StringSetEntry : public fbl::SinglyLinkedListable<
            fbl::unique_ptr<StringSetEntry>> {
        StringSetEntry(const CString& string)
            : string(string) {}

        const CString string;

        // Used by the hash table.
        CString GetKey() const { return string; }
        static size_t GetHash(CString key) {
            return fnv1a64str(key);
        }
    };

    // We want to work with C strings here, but the default implementation
    // would just compare pointers.
    struct CategoryStringKeyTraits {
        static CString GetKey(const StringSetEntry& obj) {
            return obj.string;
        }
        static bool LessThan(const CString& key1, const CString& key2) {
            return strcmp(key1, key2) < 0;
        }
        static bool EqualTo(const CString& key1, const CString& key2) {
            return strcmp(key1, key2) == 0;
        }
    };

    using StringSet = fbl::HashTable<const char*,
        fbl::unique_ptr<StringSetEntry>,
        fbl::SinglyLinkedList<fbl::unique_ptr<StringSetEntry>>, // default
        size_t, // default
        37, // default
        CategoryStringKeyTraits>;
    StringSet enabled_category_set_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(TraceHandlerImpl);
};

} // namespace internal
} // namespace trace