/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_OAT_OAT_FILE_MANAGER_H_
#define ART_RUNTIME_OAT_OAT_FILE_MANAGER_H_

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/compiler_filter.h"
#include "base/locks.h"
#include "base/macros.h"
#include "jni.h"

namespace art HIDDEN {

namespace gc {
namespace space {
class ImageSpace;
}  // namespace space
}  // namespace gc

class ClassLoaderContext;
class DexFile;
class MemMap;
class OatFile;
class ThreadPool;

// Class for dealing with oat file management.
//
// This class knows about all the loaded oat files and provides utility functions. The oat file
// pointers returned from functions are always valid.
class OatFileManager {
 public:
  OatFileManager();
  ~OatFileManager();

  // Add an oat file to the internal accounting, std::aborts if there already exists an oat file
  // with the same base address. Returns the oat file pointer from oat_file.
  // The `in_memory` parameter is whether the oat file is not present on disk,
  // but only in memory (for example files created with memfd).
  EXPORT const OatFile* RegisterOatFile(std::unique_ptr<const OatFile> oat_file,
                                        bool in_memory = false)
      REQUIRES(!Locks::oat_file_manager_lock_);

  void UnRegisterAndDeleteOatFile(const OatFile* oat_file)
      REQUIRES(!Locks::oat_file_manager_lock_);

  // Find the first opened oat file with the same location, returns null if there are none.
  EXPORT const OatFile* FindOpenedOatFileFromOatLocation(const std::string& oat_location) const
      REQUIRES(!Locks::oat_file_manager_lock_);

  // Find the oat file which contains a dex files with the given dex base location,
  // returns null if there are none.
  const OatFile* FindOpenedOatFileFromDexLocation(const std::string& dex_base_location) const
      REQUIRES(!Locks::oat_file_manager_lock_);

  // Returns the boot image oat files.
  EXPORT std::vector<const OatFile*> GetBootOatFiles() const;

  // Returns the oat files for the images, registers the oat files.
  // Takes ownership of the imagespace's underlying oat files.
  std::vector<const OatFile*> RegisterImageOatFiles(
      const std::vector<gc::space::ImageSpace*>& spaces)
      REQUIRES(!Locks::oat_file_manager_lock_);

  // Finds or creates the oat file holding dex_location. Then loads and returns
  // all corresponding dex files (there may be more than one dex file loaded
  // in the case of multidex).
  // This may return the original, unquickened dex files if the oat file could
  // not be generated.
  //
  // Returns an empty vector if the dex files could not be loaded. In this
  // case, there will be at least one error message returned describing why no
  // dex files could not be loaded. The 'error_msgs' argument must not be
  // null, regardless of whether there is an error or not.
  //
  // This method should not be called with the mutator_lock_ held, because it
  // could end up starving GC if we need to generate or relocate any oat
  // files.
  std::vector<std::unique_ptr<const DexFile>> OpenDexFilesFromOat(
      const char* dex_location,
      jobject class_loader,
      jobjectArray dex_elements,
      /*out*/ const OatFile** out_oat_file,
      /*out*/ std::vector<std::string>* error_msgs)
      REQUIRES(!Locks::oat_file_manager_lock_, !Locks::mutator_lock_);

  // Opens dex files provided in `dex_mem_maps` and attempts to find an anonymous
  // vdex file created during a previous load attempt. If found, will initialize
  // an instance of OatFile to back the DexFiles and preverify them using the
  // vdex's VerifierDeps.
  //
  // Returns an empty vector if the dex files could not be loaded. In this
  // case, there will be at least one error message returned describing why no
  // dex files could not be loaded. The 'error_msgs' argument must not be
  // null, regardless of whether there is an error or not.
  std::vector<std::unique_ptr<const DexFile>> OpenDexFilesFromOat(
      std::vector<MemMap>&& dex_mem_maps,
      jobject class_loader,
      jobjectArray dex_elements,
      /*out*/ const OatFile** out_oat_file,
      /*out*/ std::vector<std::string>* error_msgs)
      REQUIRES(!Locks::oat_file_manager_lock_, !Locks::mutator_lock_);

  void DumpForSigQuit(std::ostream& os);

  void SetOnlyUseTrustedOatFiles();
  void ClearOnlyUseTrustedOatFiles();

  // Spawn a background thread which verifies all classes in the given dex files.
  void RunBackgroundVerification(const std::vector<const DexFile*>& dex_files,
                                 jobject class_loader);

  // Wait for thread pool workers to be created. This is used during shutdown as
  // threads are not allowed to attach while runtime is in shutdown lock.
  void WaitForWorkersToBeCreated();

  // If allocated, delete a thread pool of background verification threads.
  void DeleteThreadPool();

  // Wait for any ongoing background verification tasks to finish.
  EXPORT void WaitForBackgroundVerificationTasksToFinish();

  // Wait for all background verification tasks to finish. This is only used by tests.
  EXPORT void WaitForBackgroundVerificationTasks();

  // Maximum number of anonymous vdex files kept in the process' data folder.
  static constexpr size_t kAnonymousVdexCacheSize = 8u;

  bool ContainsPc(const void* pc) REQUIRES(!Locks::oat_file_manager_lock_);

 private:
  std::vector<std::unique_ptr<const DexFile>> OpenDexFilesFromOat_Impl(
      std::vector<MemMap>&& dex_mem_maps,
      jobject class_loader,
      jobjectArray dex_elements,
      /*out*/ const OatFile** out_oat_file,
      /*out*/ std::vector<std::string>* error_msgs)
      REQUIRES(!Locks::oat_file_manager_lock_, !Locks::mutator_lock_);

  const OatFile* FindOpenedOatFileFromOatLocationLocked(const std::string& oat_location) const
      REQUIRES(Locks::oat_file_manager_lock_);

  // Return true if we should attempt to load the app image.
  bool ShouldLoadAppImage() const;

  std::set<std::unique_ptr<const OatFile>> oat_files_ GUARDED_BY(Locks::oat_file_manager_lock_);

  // Only use the compiled code in an OAT file when the file is on /system. If the OAT file
  // is not on /system, don't load it "executable".
  bool only_use_system_oat_files_;

  // Single-thread pool used to run the verifier in the background.
  std::unique_ptr<ThreadPool> verification_thread_pool_;

  DISALLOW_COPY_AND_ASSIGN(OatFileManager);
};

}  // namespace art

#endif  // ART_RUNTIME_OAT_OAT_FILE_MANAGER_H_
