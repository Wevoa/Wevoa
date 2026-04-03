#pragma once

#include <cstdint>

namespace wevoaweb {

class Interpreter;

void registerSqliteModule(Interpreter& interpreter);
void resetSqliteThreadMetrics();
std::uint64_t consumeSqliteThreadMetrics();

}  // namespace wevoaweb
