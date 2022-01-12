#include "OSCallHandler.h"

#include <cassert>

namespace fs = std::filesystem;
using namespace ZomboidHook;

constexpr auto DBFILE = "ZomboidSQLite.db";

bool OSCallHandler::ShouldIntercept(const fs::path& path) noexcept {
	auto str = path.filename().string();
	if (!str.ends_with(".bin"))
		return false;
	auto maybeSavesDir = path.parent_path().parent_path().parent_path();
	if (!maybeSavesDir.has_parent_path()) [[unlikely]]
		return false;
	return /*(str.starts_with("chunkdata_") || str.starts_with("map_") ||
						str.starts_with("zpop_")) &&*/
			maybeSavesDir.filename().string() == "Saves" &&
			fileOps.FileExists(path.parent_path());
}

bool OSCallHandler::ShouldIntercept(const FileInfo& info) noexcept {
	return filePointers.contains(info.handle) || ShouldIntercept(info.path);
}

SaveDB::SaveDB(std::filesystem::path path) :
		SQLite{
				std::move(path),
				"CREATE TABLE IF NOT EXISTS files (name TEXT PRIMARY KEY, data BLOB)"},
		getBlobRowIDStmt{
				PrepareStatement("SELECT rowid FROM files WHERE name = ?")},
		blobExistsStmt{
				PrepareStatement("SELECT COUNT(1) FROM files WHERE name = ?")},
		upsertBlobStmt{PrepareStatement(
				"INSERT OR REPLACE INTO files(name, data) VALUES(?1, ?2)")},
		blobSizeStmt{
				PrepareStatement("SELECT length(data) FROM files WHERE name = ?")},
		truncateStmt{PrepareStatement(
				"UPDATE files SET data = substr(data, ?1, ?2) WHERE name = ?3")},
		deleteStmt{
				PrepareStatement("UPDATE files SET data = NULL WHERE name = ?1")} {}

size_t SaveDB::GetBlobRowIDStmt() const noexcept {
	return getBlobRowIDStmt;
}

size_t SaveDB::BlobExistsStmt() const noexcept {
	return blobExistsStmt;
}

size_t SaveDB::UpsertBlobStmt() const noexcept {
	return upsertBlobStmt;
}

size_t SaveDB::BlobSizeStmt() const noexcept {
	return blobSizeStmt;
}

size_t SaveDB::TruncateStmt() const noexcept {
	return truncateStmt;
}

size_t SaveDB::DeleteStmt() const noexcept {
	return deleteStmt;
}

void SaveDB::OnClosed() noexcept {
	if (fs::is_empty(Path().parent_path()))
		fs::remove(Path().parent_path());
}

OSCallHandler::OSCallHandler(IFileOps& fileOps) : fileOps{fileOps} {}

bool OSCallHandler::BlobExists(SaveDB& db, const fs::path& path) {
	return db[db.BlobExistsStmt()].Execute([&](bool exists) { return exists; },
																				 path.filename().string());
}

bool OSCallHandler::BlobExists(SaveDB& db, const FileInfo& info) {
	return BlobExists(db, info.path);
}

static int BlobSize(SaveDB& db, const FileInfo& info) {
	return db[db.BlobSizeStmt()].Execute([](int len) { return len; },
																			 info.path.filename().string());
}

SaveDB& OSCallHandler::GetDBInstance(const fs::path& path) {
	auto parent = path.parent_path();
	return databases.try_emplace(parent.string(), parent / DBFILE).first->second;
}

SaveDB& OSCallHandler::GetDBInstance(const FileInfo& info) {
	return GetDBInstance(info.path);
}

FileIntent OSCallHandler::FileOpenOnly(FileInfo info) {
	if (!ShouldIntercept(info))
		return FileIntent::PASSTHRU;
	auto& db = GetDBInstance(info);
	if (BlobExists(db, info)) {
		filePointers[info.handle] = 0;
		return FileIntent::SUCCEED;
	}
	if (fileOps.FileExists(info.path)) {
		auto mmap = fileOps.MemMapFile(info.path);
		db[db.UpsertBlobStmt()].Execute(info.path.filename().string(),
																		std::make_pair(mmap->data(), mmap->size()));
		filePointers[info.handle] = 0;
		return FileIntent::SUCCEED;
	}
	return FileIntent::PASSTHRU;
}

FileIntent OSCallHandler::FileCreateOnly(FileInfo info) {
	if (!ShouldIntercept(info))
		return FileIntent::PASSTHRU;
	auto& db = GetDBInstance(info);
	if (BlobExists(db, info))
		return FileIntent::FAIL;
	if (fileOps.FileExists(
					info.path)) { // Make an internal copy anyway before we fail it.
		auto mmap = fileOps.MemMapFile(info.path);
		db[db.UpsertBlobStmt()].Execute(info.path.filename().string(),
																		std::make_pair(mmap->data(), mmap->size()));
		return FileIntent::FAIL;
	}
	return FileIntent::SUCCEED;
}

FileIntent OSCallHandler::FileOpenOrCreate(FileInfo info) {
	if (!ShouldIntercept(info))
		return FileIntent::PASSTHRU;
	auto& db = GetDBInstance(info);
	if (BlobExists(db, info))
		return FileIntent::SUCCEED;
	if (fileOps.FileExists(info.path)) {
		auto mmap = fileOps.MemMapFile(info.path);
		db[db.UpsertBlobStmt()].Execute(info.path.filename().string(),
																		std::make_pair(mmap->data(), mmap->size()));
		filePointers[info.handle] = 0;
	}
	return FileIntent::SUCCEED;
}

FileIntent OSCallHandler::FileCreateAndWipe(FileInfo info) {
	if (!ShouldIntercept(info))
		return FileIntent::PASSTHRU;
	auto& db = GetDBInstance(info);
	if (BlobExists(db, info))
		db[db.DeleteStmt()].Execute(info.path.filename().string());
	return FileIntent::SUCCEED;
}

FileIntent OSCallHandler::FileOpenOnlyAndWipe(FileInfo info) {
	if (!ShouldIntercept(info))
		return FileIntent::PASSTHRU;
	auto& db = GetDBInstance(info);
	if (BlobExists(db, info)) {
		db[db.DeleteStmt()].Execute(info.path.filename().string());
		return FileIntent::SUCCEED;
	}
	return FileIntent::FAIL;
}

FileIntent
		OSCallHandler::FileRead(FileInfo info, uint8_t* buf, uint32_t& readLen) {
	auto& db = GetDBInstance(info);
	if (!BlobExists(db, info)) [[unlikely]]
		return FileIntent::FAIL;
	return db[db.GetBlobRowIDStmt()].Execute(
			[&](std::optional<int64_t> rowid) {
				if (!rowid)
					return FileIntent::FAIL;
				SQLBlob blob{db, SaveDB::table, SaveDB::dataCol, *rowid};
				auto len	= blob.Size();
				auto& ptr = filePointers[info.handle];
				if (readLen > len - ptr) [[unlikely]]
					readLen = len - ptr;
				blob.Read(buf, ptr, readLen);
				ptr += readLen;
				return FileIntent::SUCCEED;
			},
			info.path.filename().string());

	/*db[db.GetBlobStmt()].Execute([&] (std::pair<const uint8_t*, size_t> blob) {
			auto [data, len] = blob;
			auto& ptr = filePointers[info.handle];
			if (readLen > len - ptr)
					readLen = len - ptr;
			std::copy(data + ptr, data + ptr + readLen, buf);
			ptr += readLen;
			ret = FileIntent::SUCCEED;
	}, info.path.filename().string());
	return ret;*/
}

FileIntent OSCallHandler::FileWrite(FileInfo info,
																		const uint8_t* buf,
																		uint32_t& writeLen) {
	auto& db	= GetDBInstance(info);
	auto& ptr = filePointers[info.handle];
	if (BlobExists(db, info)) {
		if (ptr > 0 || BlobSize(db, info) > 0) {
			db[db.GetBlobRowIDStmt()].Execute(
					[&](int64_t rowid) {
						SQLBlob blob{db, SaveDB::table, SaveDB::dataCol, rowid};
						auto len = blob.Size();
						if (len - ptr < writeLen) {
							auto origData = std::make_unique<uint8_t[]>(len);
							blob.Read(origData.get(), 0, len);
							db[db.UpsertBlobStmt()].Execute(info.path.filename().string(),
																							ZeroBlob{writeLen + ptr});
							blob.Reopen(db.LastInsertRowID());
							blob.Write(std::make_pair(origData.get(), len), 0);
						}
						blob.Write(std::make_pair(buf, writeLen), ptr);
						ptr += writeLen;
					},
					info.path.filename().string());
			return FileIntent::SUCCEED;
		}
	}
	db[db.UpsertBlobStmt()].Execute(info.path.filename().string(),
																	std::make_pair(buf, writeLen));
	ptr += writeLen;
	return FileIntent::SUCCEED;

	/*if (BlobExists(db, info))
			db[db.GetBlobStmt()].Execute([&] (std::pair<const uint8_t*, size_t> blob)
	{ auto [data, len] = blob; auto& ptr = filePointers[info.handle]; auto
	tmpBufLen = len - ptr < writeLen ? writeLen + ptr : len; auto tmp =
	std::make_unique<uint8_t[]>(tmpBufLen); std::copy(data, data + len,
	tmp.get()); std::copy(buf, buf + writeLen, tmp.get() + ptr);
					db[db.UpsertBlobStmt()].Execute(info.path.filename().string(),
	std::make_pair(tmp.get(), tmpBufLen)); ptr += writeLen;
			}, info.path.filename().string());
	else
			db[db.UpsertBlobStmt()].Execute(info.path.filename().string(),
	std::make_pair(buf, writeLen)); return FileIntent::SUCCEED;*/
}

FileIntent
		OSCallHandler::FileSeek(FileInfo info, SeekFrom pos, int64_t& distance) {
	auto& ptr = filePointers[info.handle];
	switch (pos) {
		case SeekFrom::CURRENT:
			ptr += distance;
			break;
		case SeekFrom::BEGIN:
			ptr = distance;
			break;
		case SeekFrom::END:
			{
				auto& db = GetDBInstance(info);
				db[db.BlobSizeStmt()].Execute([&](int len) { ptr = len + distance; },
																			info.path.filename().string());
			}
	}
	distance = ptr;
	return FileIntent::SUCCEED;
}

FileIntent OSCallHandler::FileTruncateToCursor(FileInfo info) {
	if (auto ptr = filePointers[info.handle]; ptr == 0)
		return FileTruncate(info, 0);
	auto& db							= GetDBInstance(info);
	constexpr auto offset = 0;
	db[db.TruncateStmt()].Execute(offset,
																filePointers[info.handle],
																info.path.filename().string());
	return FileIntent::SUCCEED;
}

FileIntent OSCallHandler::FileTruncate(FileInfo info, uint64_t len) {
	assert(len <= std::numeric_limits<int64_t>::max());
	auto& db							= GetDBInstance(info);
	constexpr auto offset = 0;
	if (len == 0)
		db[db.DeleteStmt()].Execute(info.path.filename().string());
	else
		db[db.GetBlobRowIDStmt()].Execute(
				[&](int64_t rowid) {
					SQLBlob blob{db, SaveDB::table, SaveDB::dataCol, rowid};
					auto blobSize = blob.Size();
					if (len < blobSize)
						db[db.TruncateStmt()].Execute(offset,
																					filePointers[info.handle],
																					info.path.filename().string());
					else if (len > blobSize) {
						auto currentData = std::make_unique<uint8_t[]>(blobSize);
						blob.Read(currentData.get(), 0, blobSize);
						db[db.UpsertBlobStmt()].Execute(
								info.path.filename().string(),
								ZeroBlob{static_cast<int64_t>(len)});
						blob.Reopen(db.LastInsertRowID());
						blob.Write(std::make_pair(currentData.get(), blobSize), offset);
					}
				},
				info.path.filename().string());
	return FileIntent::SUCCEED;

	/*db[db.GetBlobStmt()].Execute([&] (std::pair<const uint8_t*, size_t> blob) {
			auto [data, size] = blob;
			if (len < size)
					db[db.UpsertBlobStmt()].Execute(std::make_pair(data, len));
			else if (len > size){
					auto tmpBuf = std::make_unique<uint8_t[]>(len);
					std::copy(data, data + size, tmpBuf.get());
					db[db.UpsertBlobStmt()].Execute(std::make_pair(data, len));
			}
	}, info.path.filename().string());
	return FileIntent::SUCCEED;*/
}

FileIntent OSCallHandler::FileDelete(const std::filesystem::path& path) {
	if (ShouldIntercept(path)) {
		auto& db = GetDBInstance(path);
		db[db.DeleteStmt()].Execute(path.filename().string());
		return db.RowsChanged() != 0 ? FileIntent::SUCCEED : FileIntent::FAIL;
	}
	return FileIntent::PASSTHRU;
}

FileIntent OSCallHandler::FileSetAttrib(const std::filesystem::path& path) {
	return ShouldIntercept(path) ? FileIntent::SUCCEED : FileIntent::PASSTHRU;
}

FileIntent OSCallHandler::FileGetSize(FileInfo info,
																			uint64_t& sizeOut,
																			bool isStateless) {
	if (isStateless && !ShouldIntercept(info))
		return FileIntent::PASSTHRU;
	auto& db = GetDBInstance(info);
	db[db.BlobSizeStmt()].Execute([&](int size) { sizeOut = size; },
																info.path.filename().string());
	return FileIntent::SUCCEED;
}

FileAttribute OSCallHandler::FileGetAttrib(const fs::path& path) {
	if (!ShouldIntercept(path))
		return FileAttribute::PASSTHRU;
	auto& db = GetDBInstance(path);
	if (BlobExists(db, path))
		return FileAttribute::NORMAL;
	if (fileOps.FileExists(path)) {
		auto mmap = fileOps.MemMapFile(path);
		db[db.UpsertBlobStmt()].Execute(path.filename().string(),
																		std::make_pair(mmap->data(), mmap->size()));
		return FileAttribute::NORMAL;
	}
	return FileAttribute::NOT_FOUND;
}

FileTimes OSCallHandler::FileGetTimes(const std::filesystem::path& path) {
	auto& db		= GetDBInstance(path);
	auto dbFile = db.Path();
	return fileOps.GetFileTimes(path);
}

void OSCallHandler::FileClosed(FileInfo info) {
	filePointers.erase(info.handle);
}
