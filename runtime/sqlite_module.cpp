#include "runtime/sqlite_module.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sqlite3.h>

#include "interpreter/callable.h"
#include "interpreter/interpreter.h"
#include "utils/error.h"

namespace wevoaweb {

namespace {

thread_local std::uint64_t g_sqliteMicros = 0;

class SqliteTimer final {
  public:
    SqliteTimer() : started_(std::chrono::steady_clock::now()) {}

    ~SqliteTimer() {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started_);
        g_sqliteMicros += static_cast<std::uint64_t>(elapsed.count());
    }

  private:
    std::chrono::steady_clock::time_point started_;
};

class SQLiteConnection {
  public:
    explicit SQLiteConnection(std::filesystem::path path) : path_(std::move(path)) {
        const auto parent = path_.parent_path();
        if (!parent.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error) {
                throw std::runtime_error("Unable to create database directory: " + parent.string());
            }
        }

        if (sqlite3_open(path_.string().c_str(), &handle_) != SQLITE_OK) {
            const std::string message = handle_ != nullptr ? sqlite3_errmsg(handle_) : "unknown SQLite error";
            if (handle_ != nullptr) {
                sqlite3_close(handle_);
                handle_ = nullptr;
            }
            throw std::runtime_error("Unable to open SQLite database '" + path_.string() + "': " + message);
        }

        sqlite3_exec(handle_, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(handle_, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(handle_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
        sqlite3_busy_timeout(handle_, 5000);
    }

    ~SQLiteConnection() {
        if (handle_ != nullptr) {
            sqlite3_close(handle_);
        }
    }

    SQLiteConnection(const SQLiteConnection&) = delete;
    SQLiteConnection& operator=(const SQLiteConnection&) = delete;

    sqlite3* handle() const {
        return handle_;
    }

    const std::filesystem::path& path() const {
        return path_;
    }

  private:
    sqlite3* handle_ = nullptr;
    std::filesystem::path path_;
};

class SQLiteDatabase {
  public:
    explicit SQLiteDatabase(std::filesystem::path path) : path_(std::move(path)) {}

    const std::filesystem::path& path() const {
        return path_;
    }

    std::shared_ptr<SQLiteConnection> connectionForCurrentThread() const {
        const std::string key = path_.string();
        auto found = threadLocalConnections_.find(key);
        if (found != threadLocalConnections_.end()) {
            return found->second;
        }

        auto connection = std::make_shared<SQLiteConnection>(path_);
        threadLocalConnections_.insert_or_assign(key, connection);
        return connection;
    }

  private:
    std::filesystem::path path_;
    static thread_local std::unordered_map<std::string, std::shared_ptr<SQLiteConnection>> threadLocalConnections_;
};

thread_local std::unordered_map<std::string, std::shared_ptr<SQLiteConnection>> SQLiteDatabase::threadLocalConnections_;

using StatementHandle = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

StatementHandle prepareStatement(const std::shared_ptr<SQLiteConnection>& connection,
                                 const std::string& sql,
                                 const SourceSpan& span) {
    sqlite3_stmt* rawStatement = nullptr;
    if (sqlite3_prepare_v2(connection->handle(), sql.c_str(), -1, &rawStatement, nullptr) != SQLITE_OK) {
        throw RuntimeError("SQLite prepare failed: " + std::string(sqlite3_errmsg(connection->handle())), span);
    }

    return StatementHandle(rawStatement, sqlite3_finalize);
}

void bindValue(sqlite3_stmt* statement, int index, const Value& value, const SourceSpan& span) {
    int status = SQLITE_OK;

    if (value.isNil()) {
        status = sqlite3_bind_null(statement, index);
    } else if (value.isInteger()) {
        status = sqlite3_bind_int64(statement, index, value.asInteger());
    } else if (value.isBoolean()) {
        status = sqlite3_bind_int64(statement, index, value.asBoolean() ? 1 : 0);
    } else if (value.isString()) {
        status = sqlite3_bind_text(statement, index, value.asString().c_str(), -1, SQLITE_TRANSIENT);
    } else {
        throw RuntimeError("SQLite parameters must be nil, integers, booleans, or strings.", span);
    }

    if (status != SQLITE_OK) {
        throw RuntimeError("SQLite bind failed.", span);
    }
}

void bindParameters(sqlite3_stmt* statement, const Value& parameters, const SourceSpan& span) {
    if (parameters.isNil()) {
        return;
    }

    const int expectedCount = sqlite3_bind_parameter_count(statement);
    if (parameters.isArray()) {
        const auto& values = parameters.asArray();
        if (static_cast<int>(values.size()) != expectedCount) {
            throw RuntimeError("SQLite parameter count mismatch.", span);
        }

        for (std::size_t index = 0; index < values.size(); ++index) {
            bindValue(statement, static_cast<int>(index) + 1, values[index], span);
        }
        return;
    }

    if (parameters.isObject()) {
        const auto& values = parameters.asObject();
        for (int index = 1; index <= expectedCount; ++index) {
            const char* parameterName = sqlite3_bind_parameter_name(statement, index);
            if (parameterName == nullptr || *parameterName == '\0') {
                throw RuntimeError("Positional SQLite parameters require an array.", span);
            }

            std::string key(parameterName);
            if (!key.empty() && (key.front() == ':' || key.front() == '@' || key.front() == '$')) {
                key.erase(key.begin());
            }

            const auto found = values.find(key);
            if (found == values.end()) {
                throw RuntimeError("Missing SQLite named parameter '" + key + "'.", span);
            }

            bindValue(statement, index, found->second, span);
        }
        return;
    }

    throw RuntimeError("SQLite parameters must be an array or object.", span);
}

Value columnValue(sqlite3_stmt* statement, int columnIndex) {
    const int type = sqlite3_column_type(statement, columnIndex);

    switch (type) {
        case SQLITE_INTEGER:
            return Value(static_cast<std::int64_t>(sqlite3_column_int64(statement, columnIndex)));
        case SQLITE_NULL:
            return Value {};
        case SQLITE_TEXT:
            return Value(std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, columnIndex))));
        case SQLITE_FLOAT:
            return Value(std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, columnIndex))));
        case SQLITE_BLOB: {
            const auto* bytes = static_cast<const unsigned char*>(sqlite3_column_blob(statement, columnIndex));
            const int size = sqlite3_column_bytes(statement, columnIndex);
            return Value(std::string(reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(size)));
        }
        default:
            return Value {};
    }
}

Value executeExec(const std::shared_ptr<SQLiteConnection>& connection,
                  const std::string& sql,
                  const Value& parameters,
                  const SourceSpan& span) {
    SqliteTimer timer;
    auto statement = prepareStatement(connection, sql, span);
    bindParameters(statement.get(), parameters, span);

    while (true) {
        const int stepResult = sqlite3_step(statement.get());
        if (stepResult == SQLITE_DONE) {
            return Value(static_cast<std::int64_t>(sqlite3_changes(connection->handle())));
        }
        if (stepResult != SQLITE_ROW) {
            throw RuntimeError("SQLite exec failed: " + std::string(sqlite3_errmsg(connection->handle())), span);
        }
    }
}

Value executeQuery(const std::shared_ptr<SQLiteConnection>& connection,
                   const std::string& sql,
                   const Value& parameters,
                   const SourceSpan& span) {
    SqliteTimer timer;
    auto statement = prepareStatement(connection, sql, span);
    bindParameters(statement.get(), parameters, span);

    Value::Array rows;
    while (true) {
        const int stepResult = sqlite3_step(statement.get());
        if (stepResult == SQLITE_DONE) {
            break;
        }
        if (stepResult != SQLITE_ROW) {
            throw RuntimeError("SQLite query failed: " + std::string(sqlite3_errmsg(connection->handle())), span);
        }

        Value::Object row;
        const int columnCount = sqlite3_column_count(statement.get());
        for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
            row.insert_or_assign(sqlite3_column_name(statement.get(), columnIndex),
                                 columnValue(statement.get(), columnIndex));
        }

        rows.push_back(Value(std::move(row)));
    }

    return Value(std::move(rows));
}

Value executeScalar(const std::shared_ptr<SQLiteConnection>& connection,
                    const std::string& sql,
                    const Value& parameters,
                    const SourceSpan& span) {
    SqliteTimer timer;
    auto statement = prepareStatement(connection, sql, span);
    bindParameters(statement.get(), parameters, span);

    const int stepResult = sqlite3_step(statement.get());
    if (stepResult == SQLITE_DONE) {
        return Value {};
    }
    if (stepResult != SQLITE_ROW) {
        throw RuntimeError("SQLite scalar query failed: " + std::string(sqlite3_errmsg(connection->handle())), span);
    }

    return columnValue(statement.get(), 0);
}

Value makeDatabaseObject(const std::shared_ptr<SQLiteDatabase>& database) {
    Value::Object object;
    object.insert_or_assign("path", Value(database->path().generic_string()));
    object.insert_or_assign(
        "exec",
        Value(std::make_shared<NativeFunction>(
            "db.exec",
            std::nullopt,
            [database](Interpreter&, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
                if (arguments.empty() || arguments.size() > 2 || !arguments[0].isString()) {
                    throw RuntimeError("db.exec() expects a SQL string and optional parameters.", span);
                }

                const Value parameters = arguments.size() == 2 ? arguments[1] : Value {};
                return executeExec(database->connectionForCurrentThread(), arguments[0].asString(), parameters, span);
            })));
    object.insert_or_assign(
        "query",
        Value(std::make_shared<NativeFunction>(
            "db.query",
            std::nullopt,
            [database](Interpreter&, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
                if (arguments.empty() || arguments.size() > 2 || !arguments[0].isString()) {
                    throw RuntimeError("db.query() expects a SQL string and optional parameters.", span);
                }

                const Value parameters = arguments.size() == 2 ? arguments[1] : Value {};
                return executeQuery(database->connectionForCurrentThread(), arguments[0].asString(), parameters, span);
            })));
    object.insert_or_assign(
        "scalar",
        Value(std::make_shared<NativeFunction>(
            "db.scalar",
            std::nullopt,
            [database](Interpreter&, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
                if (arguments.empty() || arguments.size() > 2 || !arguments[0].isString()) {
                    throw RuntimeError("db.scalar() expects a SQL string and optional parameters.", span);
                }

                const Value parameters = arguments.size() == 2 ? arguments[1] : Value {};
                return executeScalar(database->connectionForCurrentThread(), arguments[0].asString(), parameters, span);
            })));
    return Value(std::move(object));
}

}  // namespace

void registerSqliteModule(Interpreter& interpreter) {
    Value::Object sqliteModule;
    sqliteModule.insert_or_assign(
        "open",
        Value(std::make_shared<NativeFunction>(
            "sqlite.open",
            1,
            [](Interpreter&, const std::vector<Value>& arguments, const SourceSpan& span) -> Value {
                if (!arguments[0].isString()) {
                    throw RuntimeError("sqlite.open() expects a database path string.", span);
                }

                try {
                    return makeDatabaseObject(std::make_shared<SQLiteDatabase>(arguments[0].asString()));
                } catch (const std::runtime_error& error) {
                    throw RuntimeError(error.what(), span);
                }
            })));

    interpreter.defineGlobal("sqlite", Value(std::move(sqliteModule)), true);
}

void resetSqliteThreadMetrics() {
    g_sqliteMicros = 0;
}

std::uint64_t consumeSqliteThreadMetrics() {
    const std::uint64_t total = g_sqliteMicros;
    g_sqliteMicros = 0;
    return total;
}

}  // namespace wevoaweb
