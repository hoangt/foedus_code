/*
 * Copyright (c) 2014, Hewlett-Packard Development Company, LP.
 * The license and distribution terms for this file are placed in LICENSE.txt.
 */
#ifndef FOEDUS_STORAGE_STORAGE_HPP_
#define FOEDUS_STORAGE_STORAGE_HPP_
#include <iosfwd>
#include <string>

#include "foedus/epoch.hpp"
#include "foedus/error_stack.hpp"
#include "foedus/fwd.hpp"
#include "foedus/initializable.hpp"
#include "foedus/log/fwd.hpp"
#include "foedus/snapshot/snapshot_id.hpp"
#include "foedus/storage/fwd.hpp"
#include "foedus/storage/storage_id.hpp"
#include "foedus/thread/fwd.hpp"

namespace foedus {
namespace storage {
/**
 * @brief Represents one key-value store.
 * @ingroup STORAGE
 * @details
 * This pure virtual class represents a set of key-value pairs.
 * The derived class defines \e how they are stored.
 *
 * @section ST Storage vs Tables/Indexes
 * One \e storage corresponds to one set of key-value pairs.
 * A relational \e Table consists of one or more storages, primary storage and zero or more
 * secondary storages (secondary indexes).
 * However, libfoedus-core is agnostic to relational layer. It only knows about key-value store.
 * The relation between primary and secondary storages are controlled by higher layers.
 *
 * @section INSTANTIATE Instantiating Storage
 * Storage object is instantiated in two ways.
 * To newly create a storage, the user invokes storage manager's create_xxx, which instantiates
 * this object and calls create().
 * To retrieve an existing storage, bluh bluh
 */
class Storage : public virtual Initializable {
 public:
  virtual ~Storage() {}

  /**
   * Returns the unique ID of this storage.
   */
  virtual StorageId           get_id() const = 0;

  /**
   * Returns the type of this storage.
   */
  virtual StorageType         get_type() const = 0;

  /**
   * Returns the unique name of this storage.
   */
  virtual const std::string&  get_name() const = 0;

  /**
   * Returns the metadata of this storage.
   * @return metadata for the individual storage instance. You can dynamic_cast it to
   * derived metadata object.
   */
  virtual const Metadata*     get_metadata() const = 0;

  /**
   * Returns whether this storage is already created.
   */
  virtual bool                exists() const = 0;

  /**
   * @brief Newly creates this storage and registers it in the storage manager.
   * @pre exists() == false
   * @details
   * This is invoked from storage manager's create_xxx methods.
   * Depending on the storage type, this might take a long time to finish.
   * For a newly created storage, the instasnce of this object is an empty and
   * trivial-to-instantiate (thus no exception) until we call this method.
   */
  virtual ErrorStack          create(thread::Thread* context) = 0;

  /**
   * Implementation of ostream operator.
   */
  virtual void                describe(std::ostream* o) const = 0;

  /**
   * Combines inputs for batch_sort_logs().
   */
  struct BatchSortLogInput {
    /**
     * positions of log entries relative to buffer_base_address_.
     */
    const snapshot::BufferPosition* log_positions_;
    uint32_t                        log_positions_count_;
    const char*                     buffer_base_address_;
    /**
     * For whatever purpose, the implementation can use this buffer to
     */
    void*                           sort_buffer_;
    uint64_t                        sort_buffer_size_;
    /**
     * All log entries in this inputs are assured to be after this epoch.
     * Also, it is assured to be within 2^16 from this epoch.
     * Even with 10 milliseconds per epoch, this corresponds to more than 10 hours.
     * Snapshot surely happens more often than that.
     */
    Epoch                           base_epoch_;

    inline const log::RecordLogType* resolve_position(snapshot::BufferPosition position) const {
      return reinterpret_cast<const log::RecordLogType*>(
        buffer_base_address_ + snapshot::from_buffer_position(position));
    }
  };

  /**
   * @brief Called from log reducer to sort log entries by keys.
   * @param[in] input combined input
   * @param[out] output_buffer sorted results are written to this variable.
   * the buffer size is at least of log_positions_count_.
   * @param[out] written_count how many logs written to output_buffer. If there was no compaction,
   * this will be same as log_positions_count_.
   * @details
   * All log entries passed to this method are for this storage.
   * Each storage type implements an efficient and batched way of sorting all log entries
   * by key-and-then-ordinal.
   * The implementation can do \b compaction when it is safe.
   * For example, two \e ovewrite logs on the same key's same data region can be compacted to
   * one log. In that case, written_count becomes smaller than log_positions_count_.
   */
  virtual void                batch_sort_logs(
    const BatchSortLogInput&  input,
    snapshot::BufferPosition* output_buffer,
    uint32_t                  *written_count) const = 0;

  /** Just delegates to describe(). */
  friend std::ostream& operator<<(std::ostream& o, const Storage& v);
};

/**
 * @brief Interface to instantiate a storage.
 * @ingroup STORAGE
 * @details
 * This is an interface of factory classes for storage classes.
 * One reason to have a factory class in this case is to encapsulate error handling during
 * instantiation, which is impossible if we simply invoke C++ constructors.
 */
class StorageFactory {
 public:
  virtual ~StorageFactory() {}

  /**
   * Returns the type of storages this factory creates.
   */
  virtual StorageType  get_type() const = 0;

  /**
   * @brief Tells if the given metadata object satisfies the requirement of the storage.
   * @param[in] metadata metadata object of a derived class
   * @details
   * For example, ArrayStorageFactory receive only ArrayMetadata.
   * The storage manager checks with all storage factories for each instantiation request
   * to identify the right factory class (a bit wasteful, but storage creation is a rare event).
   */
  virtual bool is_right_metadata(const Metadata *metadata) const = 0;

  /**
   * @brief Instantiate a storage object with the given metadata.
   * @param[in] metadata metadata of the newly instantiated storage object
   * @param[out] storage set only when this method succeeds. otherwise null.
   * @pre is_right_metadata(metadata)
   * @details
   * This method verifies the metadata object and might return errors for various reasons.
   */
  virtual ErrorStack get_instance(Engine* engine, const Metadata *metadata,
                                  Storage** storage) const = 0;

  /**
   * Adds a log entry for newly creating the storage to the context's log buffer.
   * @pre is_right_metadata(metadata)
   */
  virtual void add_create_log(const Metadata *metadata, thread::Thread* context) const = 0;
};
}  // namespace storage
}  // namespace foedus
#endif  // FOEDUS_STORAGE_STORAGE_HPP_
