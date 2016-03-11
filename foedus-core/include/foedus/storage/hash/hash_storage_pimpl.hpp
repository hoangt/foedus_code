/*
 * Copyright (c) 2014-2015, Hewlett-Packard Development Company, LP.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details. You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * HP designates this particular file as subject to the "Classpath" exception
 * as provided by HP in the LICENSE.txt file that accompanied this code.
 */
#ifndef FOEDUS_STORAGE_HASH_HASH_STORAGE_PIMPL_HPP_
#define FOEDUS_STORAGE_HASH_HASH_STORAGE_PIMPL_HPP_
#include <stdint.h>

#include <string>
#include <vector>

#include "foedus/attachable.hpp"
#include "foedus/compiler.hpp"
#include "foedus/cxx11.hpp"
#include "foedus/fwd.hpp"
#include "foedus/assorted/assorted_func.hpp"
#include "foedus/assorted/const_div.hpp"
#include "foedus/cache/fwd.hpp"
#include "foedus/memory/fwd.hpp"
#include "foedus/soc/shared_memory_repo.hpp"
#include "foedus/storage/fwd.hpp"
#include "foedus/storage/storage.hpp"
#include "foedus/storage/storage_id.hpp"
#include "foedus/storage/hash/fwd.hpp"
#include "foedus/storage/hash/hash_id.hpp"
#include "foedus/storage/hash/hash_metadata.hpp"
#include "foedus/storage/hash/hash_page_impl.hpp"
#include "foedus/storage/hash/hash_storage.hpp"
#include "foedus/thread/fwd.hpp"
#include "foedus/xct/fwd.hpp"

namespace foedus {
namespace storage {
namespace hash {
/** Shared data of this storage type */
struct HashStorageControlBlock final {
  // this is backed by shared memory. not instantiation. just reinterpret_cast.
  HashStorageControlBlock() = delete;
  ~HashStorageControlBlock() = delete;

  bool exists() const { return status_ == kExists || status_ == kMarkedForDeath; }
  /** @return the number of child pointers in the root page for this storage */
  uint16_t get_root_children() const {
    return assorted::int_div_ceil(bin_count_, kHashMaxBins[levels_ - 1U]);
  }

  soc::SharedMutex    status_mutex_;
  /** Status of the storage */
  StorageStatus       status_;
  /** Points to the root page (or something equivalent). */
  DualPagePointer     root_page_pointer_;
  /** metadata of this storage. */
  HashMetadata        meta_;

  // Do NOT reorder members up to here. The layout must be compatible with StorageControlBlock
  // Type-specific shared members below.

  // these are static auxiliary variables. doesn't have to be shared actually, but easier.
  /**
   * How many hash bins this storage has.
   * bin_count_ = 2^bin_bits
   */
  uint64_t            bin_count_;
  /**
   * Number of levels of pages. 1 means there is only 1 intermediate page pointing to data pages.
   * 2 means a root page pointing down to leaf intermediate pages pointing to data pages.
   * At least 1, and surely within 8 levels.
   */
  uint8_t             levels_;
  char                padding_[7];
};

/**
 * @brief Pimpl object of HashStorage.
 * @ingroup HASH
 * @details
 * A private pimpl object for HashStorage.
 * Do not include this header from a client program unless you know what you are doing.
 */
class HashStoragePimpl final : public Attachable<HashStorageControlBlock> {
 public:
  HashStoragePimpl() : Attachable<HashStorageControlBlock>() {}
  explicit HashStoragePimpl(HashStorage* storage)
    : Attachable<HashStorageControlBlock>(
      storage->get_engine(),
      storage->get_control_block()) {}

  /** Used only from uninitialize() */
  void        release_pages_recursive_root(
    memory::PageReleaseBatch* batch,
    HashIntermediatePage* page,
    VolatilePagePointer volatile_page_id);
  void        release_pages_recursive_intermediate(
    memory::PageReleaseBatch* batch,
    HashIntermediatePage* page,
    VolatilePagePointer volatile_page_id);
  void        release_pages_recursive_data(
    memory::PageReleaseBatch* batch,
    HashDataPage* page,
    VolatilePagePointer volatile_page_id);


  xct::TrackMovedRecordResult track_moved_record(
    xct::RwLockableXctId* old_address,
    xct::WriteXctAccess* write_set);
  xct::TrackMovedRecordResult track_moved_record_search(
    HashDataPage* page,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo);

  /** These are defined in hash_storage_verify.cpp */
  ErrorStack  verify_single_thread(Engine* engine);
  ErrorStack  verify_single_thread(thread::Thread* context);
  ErrorStack  verify_single_thread_intermediate(Engine* engine, HashIntermediatePage* page);
  ErrorStack  verify_single_thread_data(Engine* engine, HashDataPage* head);

  /** These are defined in hash_storage_debug.cpp */
  ErrorStack debugout_single_thread(
    Engine* engine,
    bool volatile_only,
    bool intermediate_only,
    uint32_t max_pages);
  ErrorStack debugout_single_thread_intermediate(
    Engine* engine,
    cache::SnapshotFileSet* fileset,
    HashIntermediatePage* parent,
    bool follow_volatile,
    bool intermediate_only,
    uint32_t* remaining_pages);
  ErrorStack debugout_single_thread_data(
    Engine* engine,
    cache::SnapshotFileSet* fileset,
    HashDataPage* head,
    bool follow_volatile,
    uint32_t* remaining_pages);

  ErrorStack  create(const HashMetadata& metadata);
  ErrorStack  load(const StorageControlBlock& snapshot_block);
  ErrorStack  drop();

  bool                exists()    const { return control_block_->exists(); }
  StorageId           get_id()    const { return control_block_->meta_.id_; }
  const StorageName&  get_name()  const { return control_block_->meta_.name_; }
  const HashMetadata& get_meta()  const { return control_block_->meta_; }
  uint8_t             get_levels() const { return control_block_->levels_; }
  HashBin             get_bin_count() const { return get_meta().get_bin_count(); }
  uint8_t             get_bin_bits() const { return get_meta().bin_bits_; }
  uint8_t             get_bin_shifts() const { return get_meta().get_bin_shifts(); }

  /** @see foedus::storage::hash::HashStorage::get_record() */
  ErrorCode   get_record(
    thread::Thread* context,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    void* payload,
    uint16_t* payload_capacity,
    bool read_only);

  /** @see foedus::storage::hash::HashStorage::get_record_primitive() */
  template <typename PAYLOAD>
  inline ErrorCode get_record_primitive(
    thread::Thread* context,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    PAYLOAD* payload,
    uint16_t payload_offset,
    bool read_only) {
    // at this point, there isn't enough benefit to do optimization specific to this case.
    // hash-lookup is anyway dominant. memcpy-vs-primitive is not the issue.
    return get_record_part(
      context,
      key,
      key_length,
      combo,
      payload,
      payload_offset,
      sizeof(PAYLOAD),
      read_only);
  }

  /** @see foedus::storage::hash::HashStorage::get_record_part() */
  ErrorCode   get_record_part(
    thread::Thread* context,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    void* payload,
    uint16_t payload_offset,
    uint16_t payload_count,
    bool read_only);

  /** @see foedus::storage::hash::HashStorage::insert_record() */
  ErrorCode insert_record(
    thread::Thread* context,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    const void* payload,
    uint16_t payload_count,
    uint16_t physical_payload_hint);

  /** @see foedus::storage::hash::HashStorage::delete_record() */
  ErrorCode delete_record(
    thread::Thread* context,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo);

  /** @see foedus::storage::hash::HashStorage::upsert_record() */
  ErrorCode upsert_record(
    thread::Thread* context,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    const void* payload,
    uint16_t payload_count,
    uint16_t physical_payload_hint);

  /** @see foedus::storage::hash::HashStorage::overwrite_record() */
  ErrorCode overwrite_record(
    thread::Thread* context,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    const void* payload,
    uint16_t payload_offset,
    uint16_t payload_count);

  /** @see foedus::storage::hash::HashStorage::overwrite_record_primitive() */
  template <typename PAYLOAD>
  inline ErrorCode overwrite_record_primitive(
    thread::Thread* context,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    PAYLOAD payload,
    uint16_t payload_offset) {
    // same above. still handy as an API, though.
    return overwrite_record(
      context,
      key,
      key_length,
      combo,
      &payload,
      payload_offset,
      sizeof(payload));
  }

  /** @see foedus::storage::hash::HashStorage::increment_record() */
  template <typename PAYLOAD>
  ErrorCode   increment_record(
    thread::Thread* context,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    PAYLOAD* value,
    uint16_t payload_offset);

  /**
   * Retrieves the root page of this storage.
   */
  ErrorCode   get_root_page(
    thread::Thread* context,
    bool for_write,
    HashIntermediatePage** root);
  /** for non-root */
  ErrorCode   follow_page(
    thread::Thread* context,
    bool for_write,
    HashIntermediatePage* parent,
    uint16_t index_in_parent,
    Page** page);
  /** subroutine to follow a pointer to head of bin from a volatile parent */
  ErrorCode   follow_page_bin_head(
    thread::Thread* context,
    bool for_write,
    HashIntermediatePage* parent,
    uint16_t index_in_parent,
    Page** page);

  /**
   * @brief Find a pointer to the bin that contains records for the hash.
   * @param[in] context Thread context
   * @param[in] for_write Whether we are reading these pages to modify
   * @param[in] combo Hash values.
   * @param[out] bin_head Pointer to the first data page of the bin. Might be null.
   * @details
   * If the search is for-write search, we always create a volatile page, even recursively,
   * thus *bin_head!= null.
   *
   * If the search is a for-read search and also the corresponding data page or its ascendants
   * do not exist yet, *bin_head returns null.
   * In that case, you can just return "not found" as a result.
   * locate_bin() internally adds a pointer set to protect the result, too.
   *
   * @note Although the above rule is simple, this might be wasteful in some situation.
   * Deletions and updates for non-existing keys do not need to instantiate volatile pages.
   * It can be handled like read access in that case.
   * But, to really achieve it, delete/update have to be 2-path. Search it using snapshot pages,
   * then goes through volatile path when found.
   * Rather, let's always create volatile pages for all writes. It's simple and performs
   * the best except a very unlucky case. If miss-delete/update is really the common path,
   * the user code can manually achieve the same thing by get() first, then delete/update().
   */
  ErrorCode   locate_bin(
    thread::Thread* context,
    bool for_write,
    const HashCombo& combo,
    HashDataPage** bin_head);

  /**
   * @brief return value of locate_record().
   * @details
   * @par Notes on XID, MOCC Locking, read-set, and "physical_only"
   * This object also contains \e observed_, which is guaranteed to be the XID at or before
   * the return from locate_record(). There is a notable contract here.
   * When locking in MOCC kicks in, we want to make sure we return the value of XID \e after
   * the lock to eliminate a chance of false verification failure.
   * However, this means we need to take lock and register read-set \e within locate_record().
   * This caused us headaches for the following reason.
   *
   * @par Logical vs Physical search in hash pages
   * \e Physically finding a record for given key in our hash page has the following guarantees.
   * \li When it finds a record, the record was at least at some point a valid
   * (non-moved/non-being-written) record of the given key.
   * \li When it doesn't find any record, the page(s) at least at some point didn't contain
   * any valid record of the given key.
   *
   * On the other hand, it still allows the following behaviors:
   * \li The found record is now being moved or modified.
   * \li The page or its next page(s) is now newly inserting a physical record of the given key.
   *
   * \e Logically finding a record provides additional guarantee to protect against the above
   * at pre-commit time. It additionally takes read-set, page-version set, or even MOCC locks.
   *
   * @par locate_record(): Logical or Physical
   * We initially designed locate_record() as a physical-only search operation
   * separated from logical operations (e.g., get_record/overwrite_record, or the caller).
   * Separation makes each of them simpler and more flexible.
   * The logical side can separately decide, using an operation-specific logic, whether it
   * will add the XID observed in locate_record() to read-set or not.
   * The physical side (locate_record()) can be dumb simple, too.
   *
   * But, seems like this is now unavoidable.
   * Hence now locate_record() is a logical operation.
   * To allow the caller to choose what to do logically, we receive
   * a paramter \e physical_only. When false, locate_record \e might take logical lock and add
   * the XID to readset, hence what locate_record() observed is protected.
   * When true, locate_record never takes lock or adds it to readset.
   * It just physically locates a record that was at least at some point a valid record.
   * In this case,
   * the caller is responsible to do appropriate thing to protect from concurrent modifications.
   */
  struct RecordLocation {
    /** Address of the slot. null if not found. */
    HashDataPage::Slot* slot_;
    /** Address of the record. null if not found. */
    char* record_;
    /**
     * TID as of locate_record() identifying the record.
     * guaranteed to be not is_moved (then automatically retried), though the \b current
     * TID might be now moved, in which case pre-commit will catch it.
     * See the class comment.
     */
    xct::XctId observed_;

    void clear() {
      slot_ = nullptr;
      record_ = nullptr;
      observed_.data_ = 0;
    }

    void populate(
      DataPageSlotIndex index,
      const void* key,
      uint16_t key_length,
      const HashCombo& combo,
      HashDataPage* page);
  };

  /**
   * @brief Usually follows locate_bin to locate the exact physical record for the key, or
   * create a new one if not exists (only when for_write).
   * @param[in] context Thread context
   * @param[in] for_write Whether we are seeking the record to modify
   * @param[in] physical_only If true, we skip observing XID and registering readset
   * in a finalized fashion, see RecordLocation.
   * @param[in] create_if_notfound Whether we will create a new physical record if not exists
   * @param[in] create_payload_length If this method creates a physical record, it makes sure
   * the record can accomodate at least this size of payload.
   * @param[in] combo Hash values. Also the result of this method.
   * @param[in] bin_head Pointer to the first data page of the bin.
   * @param[out] result Information on the found slot.
   * @param[out] read_set_address If this method took a read-set on the returned record,
   * points to the corresponding read-set. Otherwise nullptr.
   * @pre bin_head->get_bin() == combo.bin_
   * @pre bin_head != nullptr
   * @pre bin_head must be volatile page if for_write
   * @details
   * If the exact record is not found, this method protects the result by adding page version set
   * if physical_only is false.
   * If create_if_notfound is true (an insert case), this method creates a new physical record for
   * the key with a deleted-state as a system transaction.
   * @see RecordLocation
   */
  ErrorCode   locate_record(
    thread::Thread* context,
    bool for_write,
    bool physical_only,
    bool create_if_notfound,
    uint16_t create_payload_length,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    HashDataPage* bin_head,
    RecordLocation* result,
    xct::ReadXctAccess** read_set_address);

  /** locate_record()'s physical_only version. Invoke this rather than locate_record directly */
  ErrorCode   locate_record_physical_only(
    thread::Thread* context,
    bool for_write,
    bool create_if_notfound,
    uint16_t create_payload_length,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    HashDataPage* bin_head,
    RecordLocation* result) {
    xct::ReadXctAccess* dummy;
    return locate_record(
      context,
      for_write,
      true,
      create_if_notfound,
      create_payload_length,
      key,
      key_length,
      combo,
      bin_head,
      result,
      &dummy);  // physical-only search doesn't care read-set protection
  }
  /** locate_record()'s logical+physical version. Invoke this rather than locate_record directly */
  ErrorCode   locate_record_logical(
    thread::Thread* context,
    bool for_write,
    bool create_if_notfound,
    uint16_t create_payload_length,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    HashDataPage* bin_head,
    RecordLocation* result,
    xct::ReadXctAccess** read_set_address) {
    return locate_record(
      context,
      for_write,
      false,
      create_if_notfound,
      create_payload_length,
      key,
      key_length,
      combo,
      bin_head,
      result,
      read_set_address);
  }

  /** Simpler version of locate_record for when we are in snapshot world. */
  ErrorCode locate_record_in_snapshot(
    thread::Thread* context,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    HashDataPage* bin_head,
    RecordLocation* result);

  /**
   * Subroutine of locate_record() to create/migrate a physical record of the given key in the page
   * or its next pages.
   * This method has the following possible outcomes:
   * \li Created a new physical record of the key in the given page or its next pages.
   * \li Did nothing because we found an existing record of the key in the given page or its next
   * pages.
   *
   * In either case, new_location returns the index of the record, thus it's never a kSlotNotFound.
   *
   * This method is \e physical-only. It doesn't add any read-set or take logical record locks.
   * Thus, even \e seemingly right after calling this method, you might find the new_location
   * points to a moved record. It might happen. The caller is responsible to do a logical
   * lock/readset etc and retries if necessary. But, this method does guarantee that the
   * new_location points to a record of the given key that was at least at some point valid.
   */
  ErrorCode   locate_record_reserve_physical(
    thread::Thread* context,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    uint16_t payload_length,
    HashDataPage** page_in_out,
    uint16_t examined_records,
    DataPageSlotIndex* new_location);

  /**
   * @brief Moves a physical record to a later position.
   * @details
   * This method moves an existing record to a larger index in the same page, or to a next
   * page if this page is already full or has next page.
   * This method constituites a system transaction that logically does nothing, either
   * the record is deleted or not.
   * If the record is already migrated because of concurrent threads, this method does nothing
   * but returning the new location.
   *
   * @par Concurrency Control
   * Assuming that record migration doesn't happen that often, we do lots of page-locks
   * in this function. The order of the locking is:
   * \li lock cur_page, which finalizes the existence of the key and next-page in cur_page.
   * \li lock cur_index, which finalizes the state and the current record.
   * \li lock tail_page (if moves on to next page)
   * \li create a new record in tail_page, \b rel-barrier, \b then set is_moved flag in cur_slot.
   * (this means there is no moment where an is_moved record does not have a new location.
   * otherwise we have to spin for the new record in other places.)
   * \li unlock in reverse order.
   *
   * @par Invariants for Deadlock Avoidance
   * This method must be called where the calling thread has no locks either on pages or on records
   * in this hash bucket. Assuming that, this method has no chance of deadlock.
   *
   * @par Contracts and Non-Contracts
   * \li Contract: The new location is always set as far as kErrorCodeOk is returned.
   * Only possible error codes are out-of-freepages and other serious errors.
   * \li Contract: The new location is of the exact given key.
   * \li Non-Contract: Might be moved again.
   * This method does \b NOT promise that the returned location is not moved.
   * The caller must call it again in that case.
   * \li Non-Contract: The new location might \b NOT have a sufficient space.
   * The caller must call it again in that case.
   */
  ErrorCode migrate_record(
    thread::Thread* context,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    HashDataPage* cur_page,
    DataPageSlotIndex cur_index,
    uint16_t payload_count,
    RecordLocation* new_location);
  /** Subroutine of migrate_record() to implement the 4th step above. */
  void migrate_record_move(
    thread::Thread* context,
    const void* key,
    uint16_t key_length,
    const HashCombo& combo,
    HashDataPage* cur_page,
    DataPageSlotIndex cur_index,
    HashDataPage* tail_page,
    uint16_t payload_count,
    RecordLocation* new_location);

  /** Appends a next page to a given already-locked volatile data page. */
  ErrorCode append_next_volatile_page(
    thread::Thread* context,
    HashDataPage* page,
    PageVersionLockScope* scope,
    HashDataPage** next_page);
};
static_assert(sizeof(HashStoragePimpl) <= kPageSize, "HashStoragePimpl is too large");
static_assert(
  sizeof(HashStorageControlBlock) <= soc::GlobalMemoryAnchors::kStorageMemorySize,
  "HashStorageControlBlock is too large.");
}  // namespace hash
}  // namespace storage
}  // namespace foedus
#endif  // FOEDUS_STORAGE_HASH_HASH_STORAGE_PIMPL_HPP_
