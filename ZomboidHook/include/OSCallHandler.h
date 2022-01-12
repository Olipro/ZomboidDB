#pragma once

#include <string>
#include <unordered_map>

#include "SQLite.h"
#include "interface/IFileOps.h"
#include "interface/IOSCallHandler.h"

namespace ZomboidHook {
	class SaveDB : public SQLite {
		size_t getBlobRowIDStmt;
		size_t blobExistsStmt;
		size_t upsertBlobStmt;
		size_t blobSizeStmt;
		size_t truncateStmt;
		size_t deleteStmt;

	public:
		static constexpr const char* table	 = "files";
		static constexpr const char* dataCol = "data";
		explicit SaveDB(std::filesystem::path path);
		size_t GetBlobRowIDStmt() const noexcept;
		size_t BlobExistsStmt() const noexcept;
		size_t UpsertBlobStmt() const noexcept;
		size_t BlobSizeStmt() const noexcept;
		size_t TruncateStmt() const noexcept;
		size_t DeleteStmt() const noexcept;
		void OnClosed() noexcept override;
	};
	class OSCallHandler : public IOSCallHandler {
		std::unordered_map<std::string, SaveDB> databases;
		std::unordered_map<int64_t, int64_t> filePointers;
		IFileOps& fileOps;

		static bool BlobExists(SaveDB& db, const std::filesystem::path& path);
		static bool BlobExists(SaveDB& db, const FileInfo& info);
		bool ShouldIntercept(const std::filesystem::path& path) noexcept;
		bool ShouldIntercept(const FileInfo& info) noexcept;
		SaveDB& GetDBInstance(const std::filesystem::path& path);
		SaveDB& GetDBInstance(const FileInfo& info);

	public:
		explicit OSCallHandler(IFileOps& fileOps);
		[[nodiscard]] FileIntent FileOpenOnly(FileInfo info) override;
		[[nodiscard]] FileIntent FileCreateOnly(FileInfo info) override;
		[[nodiscard]] FileIntent FileOpenOrCreate(FileInfo info) override;
		[[nodiscard]] FileIntent FileCreateAndWipe(FileInfo info) override;
		[[nodiscard]] FileIntent FileOpenOnlyAndWipe(FileInfo info) override;
		[[nodiscard]] FileIntent
				FileRead(FileInfo info, uint8_t* buf, uint32_t& readLen) override;
		[[nodiscard]] FileIntent FileWrite(FileInfo info,
																			 const uint8_t* buf,
																			 uint32_t& writeLen) override;
		[[nodiscard]] FileIntent
				FileSeek(FileInfo info, SeekFrom pos, int64_t& distance) override;
		[[nodiscard]] FileIntent FileTruncateToCursor(FileInfo info) override;
		[[nodiscard]] FileIntent FileTruncate(FileInfo info, uint64_t len) override;
		[[nodiscard]] FileIntent
				FileDelete(const std::filesystem::path& path) override;
		[[nodiscard]] FileIntent
				FileSetAttrib(const std::filesystem::path& path) override;
		[[nodiscard]] FileIntent FileGetSize(FileInfo info,
																				 uint64_t& sizeOut,
																				 bool isStateless) override;
		[[nodiscard]] FileAttribute
				FileGetAttrib(const std::filesystem::path& path) override;
		[[nodiscard]] FileTimes
				FileGetTimes(const std::filesystem::path& path) override;
		void FileClosed(FileInfo info) override;
	};
} // namespace ZomboidHook
