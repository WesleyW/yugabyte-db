// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#ifndef YB_FS_FS_MANAGER_H
#define YB_FS_FS_MANAGER_H

#include <iosfwd>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest_prod.h>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/ref_counted.h"
#include "yb/util/env.h"
#include "yb/util/path_util.h"
#include "yb/util/metrics.h"

DECLARE_bool(enable_data_block_fsync);

namespace google {
namespace protobuf {
class Message;
} // namespace protobuf
} // namespace google

namespace yb {

class MemTracker;
class MetricEntity;

namespace fs {
class BlockManager;
class ReadableBlock;
class WritableBlock;
} // namespace fs

namespace itest {
class ExternalMiniClusterFsInspector;
}

class BlockId;
class InstanceMetadataPB;

struct FsManagerOpts {
  FsManagerOpts();
  ~FsManagerOpts();

  // The entity under which all metrics should be grouped. If NULL, metrics
  // will not be produced.
  //
  // Defaults to NULL.
  scoped_refptr<MetricEntity> metric_entity;

  // The memory tracker under which all new memory trackers will be parented.
  // If NULL, new memory trackers will be parented to the root tracker.
  std::shared_ptr<MemTracker> parent_mem_tracker;

  // The paths where WALs will be stored. Cannot be empty.
  std::vector<std::string> wal_paths;

  // The paths where data blocks will be stored. Cannot be empty.
  std::vector<std::string> data_paths;

  // Whether or not read-write operations should be allowed. Defaults to false.
  bool read_only;

  // This field is to be used as a path component for all the fs roots. For now, we expect it to be
  // either master or tserver.
  std::string server_type;
};

// FsManager provides helpers to read data and metadata files,
// and it's responsible for abstracting the file-system layout.
//
// The user should not be aware of where files are placed,
// but instead should interact with the storage in terms of "open the block xyz"
// or "write a new schema metadata file for table kwz".
//
// The current layout is:
//    <yb.root.dir>/data/
//    <yb.root.dir>/data/<prefix-0>/<prefix-2>/<prefix-4>/<name>
class FsManager {
 public:
  static const char *kWalDirName;
  static const char *kWalFileNamePrefix;
  static const char *kWalsRecoveryDirSuffix;
  static const char *kRocksDBDirName;

  // Only for unit tests.
  FsManager(Env* env, const std::string& root_path, const std::string& server_type);

  FsManager(Env* env, const FsManagerOpts& opts);
  ~FsManager();

  // Initialize and load the basic filesystem metadata.
  // If the file system has not been initialized, returns NotFound.
  // In that case, CreateInitialFileSystemLayout may be used to initialize
  // the on-disk structures.
  CHECKED_STATUS Open();

  //
  // Returns an error if the file system is already initialized.
  CHECKED_STATUS CreateInitialFileSystemLayout();

  void DumpFileSystemTree(std::ostream& out);

  // Return the UUID persisted in the local filesystem. If Open()
  // has not been called, this will crash.
  const std::string& uuid() const;

  // ==========================================================================
  //  Data read/write interfaces
  // ==========================================================================

  // Creates a new anonymous block.
  //
  // Block will be synced on close.
  CHECKED_STATUS CreateNewBlock(gscoped_ptr<fs::WritableBlock>* block);

  CHECKED_STATUS OpenBlock(const BlockId& block_id,
                   gscoped_ptr<fs::ReadableBlock>* block);

  CHECKED_STATUS DeleteBlock(const BlockId& block_id);

  bool BlockExists(const BlockId& block_id) const;

  // ==========================================================================
  //  on-disk path
  // ==========================================================================
  std::vector<std::string> GetDataRootDirs() const;

  std::vector<std::string> GetWalRootDirs() const;

  // Used for tests only. If GetWalRootDirs returns an empty vector, we will crash the process.
  std::string GetFirstTabletWalDirOrDie(const std::string& table_id,
                                        const std::string& tablet_id) const;

  std::string GetTabletWalRecoveryDir(const std::string& tablet_wal_path) const;

  std::string GetWalSegmentFileName(const std::string& tablet_wal_path,
                                    uint64_t sequence_number) const;

  // Return the directory where tablet superblocks should be stored.
  std::string GetTabletMetadataDir() const;

  // Return the path for a specific tablet's superblock.
  std::string GetTabletMetadataPath(const std::string& tablet_id) const;

  // List the tablet IDs in the metadata directory.
  CHECKED_STATUS ListTabletIds(std::vector<std::string>* tablet_ids);

  // Return the path where InstanceMetadataPB is stored.
  std::string GetInstanceMetadataPath(const std::string& root) const;

  // Return the directory where the consensus metadata is stored.
  std::string GetConsensusMetadataDir() const;

  // Return the path where ConsensusMetadataPB is stored.
  std::string GetConsensusMetadataPath(const std::string& tablet_id) const {
    return JoinPathSegments(GetConsensusMetadataDir(), tablet_id);
  }

  Env *env() { return env_; }

  bool read_only() const {
    return read_only_;
  }

  // ==========================================================================
  //  file-system helpers
  // ==========================================================================
  bool Exists(const std::string& path) const {
    return env_->FileExists(path);
  }

  CHECKED_STATUS ListDir(const std::string& path, std::vector<std::string> *objects) const {
    return env_->GetChildren(path, objects);
  }

  CHECKED_STATUS CreateDirIfMissing(const std::string& path, bool* created = NULL);

  fs::BlockManager* block_manager() {
    return block_manager_.get();
  }

 private:
  FRIEND_TEST(FsManagerTestBase, TestDuplicatePaths);
  friend class itest::ExternalMiniClusterFsInspector; // for access to directory names

  // Initializes, sanitizes, and canonicalizes the filesystem roots.
  CHECKED_STATUS Init();

  // Select and create an instance of the appropriate block manager.
  //
  // Does not actually perform any on-disk operations.
  void InitBlockManager();

  // Create a new InstanceMetadataPB.
  void CreateInstanceMetadata(InstanceMetadataPB* metadata);

  // Save a InstanceMetadataPB to the filesystem.
  // Does not mutate the current state of the fsmanager.
  CHECKED_STATUS WriteInstanceMetadata(
      const InstanceMetadataPB& metadata,
      const std::string& path);

  // Checks if 'path' is an empty directory.
  //
  // Returns an error if it's not a directory. Otherwise, sets 'is_empty'
  // accordingly.
  CHECKED_STATUS IsDirectoryEmpty(const std::string& path, bool* is_empty);

  // ==========================================================================
  //  file-system helpers
  // ==========================================================================
  void DumpFileSystemTree(std::ostream& out,
                          const std::string& prefix,
                          const std::string& path,
                          const std::vector<std::string>& objects);

  static const char *kDataDirName;
  static const char *kTabletMetadataDirName;
  static const char *kCorruptedSuffix;
  static const char *kInstanceMetadataFileName;
  static const char *kInstanceMetadataMagicNumber;
  static const char *kTabletSuperBlockMagicNumber;
  static const char *kConsensusMetadataDirName;

  Env *env_;

  // If false, operations that mutate on-disk state are prohibited.
  const bool read_only_;

  // These roots are the constructor input verbatim. None of them are used
  // as-is; they are first canonicalized during Init().
  const std::vector<std::string> wal_fs_roots_;
  const std::vector<std::string> data_fs_roots_;
  const std::string server_type_;

  scoped_refptr<MetricEntity> metric_entity_;

  std::shared_ptr<MemTracker> parent_mem_tracker_;

  // Canonicalized forms of 'wal_fs_roots_ and 'data_fs_roots_'. Constructed
  // during Init().
  //
  // - The first data root is used as the metadata root.
  // - Common roots in the collections have been deduplicated.
  std::set<std::string> canonicalized_wal_fs_roots_;
  std::string canonicalized_metadata_fs_root_;
  std::set<std::string> canonicalized_data_fs_roots_;
  std::set<std::string> canonicalized_all_fs_roots_;

  gscoped_ptr<InstanceMetadataPB> metadata_;

  gscoped_ptr<fs::BlockManager> block_manager_;

  bool initted_;

  DISALLOW_COPY_AND_ASSIGN(FsManager);
};

} // namespace yb

#endif  // YB_FS_FS_MANAGER_H
