#pragma once

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

#include "sqlite3.h"

namespace ZomboidHook {
	template <typename>
	struct IsCallable : std::false_type {};

	template <typename R, typename... Args>
	struct IsCallable<R(Args...)> : std::true_type {
		using return_type = R;
	};

	template <typename R, typename T, typename... Args>
	struct IsCallable<R (T::*)(Args...)> : std::true_type {
		using return_type = R;
	};

	template <typename R, typename T, typename... Args>
	struct IsCallable<R (T::*)(Args...) const> : std::true_type {
		using return_type = R;
	};

	template <typename T, typename U>
	inline constexpr bool IsSameOrOptional = (std::same_as<T, U> ||
																						std::same_as<T, std::optional<U>>);

	template <typename Clbk, typename... Args>
			requires IsCallable<Clbk>::value ||
			IsCallable<decltype(&Clbk::operator())>::value struct SQLColFetcher {
		Clbk&& clbk;

		template <typename T>
		constexpr auto GetColumn(sqlite3_stmt* stmt, int i) {
			if constexpr (IsSameOrOptional<T, std::pair<const uint8_t*, size_t>>)
				return std::pair{
						static_cast<const uint8_t*>(sqlite3_column_blob(stmt, i)),
						sqlite3_column_bytes(stmt, i)};
			else if constexpr (IsSameOrOptional<T, bool>)
				return sqlite3_column_int(stmt, i) != 0;
			else if constexpr (IsSameOrOptional<T, int>)
				return sqlite3_column_int(stmt, i);
			else if constexpr (IsSameOrOptional<T, int64_t>)
				return sqlite3_column_int64(stmt, i);
		}

		explicit constexpr SQLColFetcher(Clbk&& clbk) :
				clbk{std::forward<Clbk>(clbk)} {}
		auto operator()(sqlite3_stmt* stmt) {
			int i = 0;
			return clbk(GetColumn<Args>(stmt, i++)...);
		}
	};

	template <typename>
	struct RowFetcher;
	template <typename R, typename... Args>
	struct RowFetcher<R(Args...)> : SQLColFetcher<R (*)(Args...), Args...> {
		using SQLColFetcher<R (*)(Args...), Args...>::SQLColFetcher;
	};
	template <typename T, typename R, typename... Args>
	struct RowFetcher<R (T::*)(Args...)> : SQLColFetcher<T, Args...> {
		using SQLColFetcher<T, Args...>::SQLColFetcher;
	};
	template <typename T, typename R, typename... Args>
	struct RowFetcher<R (T::*)(Args...) const> : SQLColFetcher<T, Args...> {
		using SQLColFetcher<T, Args...>::SQLColFetcher;
	};

	template <typename Clbk>
	RowFetcher(Clbk&&) -> RowFetcher<decltype(&Clbk::operator())>;

	template <typename Clbk>
	concept is_callable = IsCallable<decltype(&Clbk::operator())>::value;
	template <typename Clbk>
	concept is_void_r = std::same_as<
			typename IsCallable<decltype(&Clbk::operator())>::return_type,
			void>;

	template <typename>
	constexpr bool all_params_optional = false;
	template <typename R, typename... Args>
	requires(
			std::same_as<
					Args,
					std::optional<
							typename Args::
									value_type>>&&...) constexpr bool all_params_optional<R(Args...)> =
			true;

	struct ZeroBlob {
		int64_t size;
	};

	class SQLStatement {
		sqlite3_stmt* stmt = nullptr;
		std::mutex mutex;

		constexpr void BindArg(std::string_view arg, int i) {
			if (SQLITE_OK !=
					sqlite3_bind_text(stmt, i, arg.data(), arg.size(), nullptr))
					[[unlikely]]
				throw std::runtime_error{"Failed to bind"};
		}

		constexpr void BindArg(std::pair<const uint8_t*, size_t> arg, int i) {
			if (SQLITE_OK !=
					sqlite3_bind_blob(stmt, i, arg.first, arg.second, nullptr))
					[[unlikely]]
				throw std::runtime_error{"Failed to bind blob"};
		}

		constexpr void BindArg(int arg, int i) {
			if (SQLITE_OK != sqlite3_bind_int(stmt, i, arg)) [[unlikely]]
				throw std::runtime_error{"failed to bind int"};
		}

		constexpr void BindArg(ZeroBlob blob, int i) {
			if (SQLITE_OK != sqlite3_bind_zeroblob64(stmt, i, blob.size)) [[unlikely]]
				throw std::runtime_error{"Failed to bind zeroblob"};
		}

		class Resetter {
			sqlite3_stmt* stmt;

		public:
			explicit Resetter(sqlite3_stmt* stmt) : stmt{stmt} {}
			~Resetter() {
				sqlite3_reset(stmt);
			}
		};

	public:
		SQLStatement(sqlite3* db, std::string_view query);
		SQLStatement(SQLStatement&& rhs) noexcept;

		template <typename Clbk, typename... Args>
				requires is_callable<Clbk> ||
				(!is_void_r<Clbk> &&
				 all_params_optional<Clbk>) auto Execute(Clbk&& clbk, Args&&... args) {
			auto i = 1;
			std::lock_guard l{mutex};
			Resetter r{stmt};
			(BindArg(std::forward<Args>(args), i++), ...);
			switch (sqlite3_step(stmt)) {
				case SQLITE_ROW:
					return RowFetcher{std::forward<Clbk>(clbk)}(stmt);
					break;
				case SQLITE_DONE:
					if constexpr (!is_void_r<Clbk>)
						throw std::logic_error{
								"void callback issued SQL query with no result"};
					break;
				default:
					throw std::runtime_error{"Failed exec: "};
			}
		}

		template <typename... Args>
		auto Execute(Args&&... args) {
			return Execute([]() {}, std::forward<Args>(args)...);
		}
		~SQLStatement();
	};

	class SQLConn {
		sqlite3* db = nullptr;

	public:
		explicit SQLConn(const std::filesystem::path& path);
		SQLConn(SQLConn&& rhs) noexcept;
		operator sqlite3*() const noexcept;
		~SQLConn();
	};

	template <typename T>
	requires std::invocable<decltype(&T::OnClosed), T&> class OnClosed {
		T& onClosed;

	public:
		explicit OnClosed(T& clbk) noexcept : onClosed{clbk} {}
		~OnClosed() {
			onClosed.OnClosed();
		}
	};

	class SQLBlob;
	class SQLite {
		virtual void OnClosed() noexcept = 0;
		friend class ::ZomboidHook::OnClosed<SQLite>;
		friend class ::ZomboidHook::SQLBlob;

		ZomboidHook::OnClosed<SQLite> onClosed{*this}; // do not reorder,
		SQLConn conn;
		std::filesystem::path path;
		std::vector<SQLStatement> statements;

		operator sqlite3*() noexcept;

	public:
		explicit SQLite(std::filesystem::path path, std::string_view schema = "");
		size_t PrepareStatement(std::string_view query);
		void Execute(std::string_view query);
		[[nodiscard]] const std::filesystem::path& Path() const noexcept;
		[[nodiscard]] sqlite3_int64 LastInsertRowID() const noexcept;
		[[nodiscard]] int RowsChanged() const noexcept;
		SQLStatement& operator[](size_t idx) noexcept;
	};

	class SQLBlob {
		sqlite3_blob* blob = nullptr;

	public:
		SQLBlob(SQLite& db, const char* table, const char* col, sqlite3_int64 row);
		SQLBlob(const SQLite&) = delete;
		SQLBlob(SQLite&&)			 = delete;
		void Read(uint8_t* buf, size_t offset, size_t len) const;
		void Write(std::pair<const uint8_t*, size_t>, size_t offset);
		void Reopen(sqlite3_int64 rowID);
		[[nodiscard]] size_t Size() const noexcept;
		~SQLBlob();
	};
} // namespace ZomboidHook