#include "SQLite.h"

#include <cassert>
#include <string>

using namespace std::string_literals;

using namespace ZomboidHook;

SQLStatement::SQLStatement(sqlite3* db, std::string_view query) {
	if (SQLITE_OK != sqlite3_prepare_v3(db,
																			query.data(),
																			query.size(),
																			SQLITE_PREPARE_PERSISTENT,
																			&stmt,
																			nullptr))
		throw std::runtime_error{"Failed to prepare statement: "s.append(query)};
}

SQLStatement::SQLStatement(SQLStatement&& rhs) noexcept : stmt{rhs.stmt} {
	rhs.stmt = nullptr;
}

SQLStatement::~SQLStatement() {
	sqlite3_finalize(stmt);
}

SQLConn::SQLConn(const std::filesystem::path& path) {
	if (SQLITE_OK != sqlite3_open_v2(path.string().c_str(),
																	 &db,
																	 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
																	 "win32-longpath")) [[unlikely]]
		throw std::runtime_error{"Failed to open DB"s + sqlite3_errmsg(db)};
}

SQLConn::SQLConn(SQLConn&& rhs) noexcept : db{rhs.db} {
	rhs.db = nullptr;
}

SQLConn::operator sqlite3*() const noexcept {
	return db;
}

SQLConn::~SQLConn() {
	sqlite3_exec(db, "VACUUM", nullptr, nullptr, nullptr);
	sqlite3_close(db);
}

SQLite::SQLite(std::filesystem::path path, std::string_view schema) :
		conn{path}, path{std::move(path)} {
	if (!schema.empty())
		Execute(schema);
	Execute("PRAGMA journal_mode=wal");
}

SQLite::operator sqlite3*() noexcept {
	return conn;
}

size_t SQLite::PrepareStatement(std::string_view query) {
	statements.emplace_back(conn, query);
	return statements.size() - 1;
}

void SQLite::Execute(std::string_view query) {
	sqlite3_exec(conn, query.data(), nullptr, nullptr, nullptr);
}

const std::filesystem::path& SQLite::Path() const noexcept {
	return path;
}

int64_t SQLite::LastInsertRowID() const noexcept {
	return sqlite3_last_insert_rowid(conn);
}

int SQLite::RowsChanged() const noexcept {
	return sqlite3_changes(conn);
}

SQLStatement& SQLite::operator[](size_t idx) noexcept {
	return statements[idx];
}

SQLBlob::SQLBlob(SQLite& db,
								 const char* table,
								 const char* col,
								 sqlite3_int64 row) {
	if (SQLITE_OK != sqlite3_blob_open(db, "main", table, col, row, 1, &blob))
			[[unlikely]]
		throw std::runtime_error{"Failed to open blob"};
}

void SQLBlob::Read(uint8_t* buf, size_t offset, size_t len) const {
	assert(len <= std::numeric_limits<int>::max());
	if (sqlite3_blob_read(blob, buf, static_cast<int>(len), offset)) [[unlikely]]
		throw std::runtime_error{"Failed to read blob"};
}

void SQLBlob::Write(std::pair<const uint8_t*, size_t> buf, size_t offset) {
	auto [data, len] = buf;
	assert(len <= std::numeric_limits<int>::max());
	if (SQLITE_OK !=
			sqlite3_blob_write(blob, data, static_cast<int>(len), offset))
			[[unlikely]]
		throw std::runtime_error{"Failed to write blob"};
}

void SQLBlob::Reopen(sqlite3_int64 rowID) {
	if (SQLITE_OK != sqlite3_blob_reopen(blob, rowID)) [[unlikely]]
		throw std::runtime_error{"Failed to reopen blob"};
}

size_t SQLBlob::Size() const noexcept {
	return sqlite3_blob_bytes(blob);
}

SQLBlob::~SQLBlob() {
	sqlite3_blob_close(blob);
}
